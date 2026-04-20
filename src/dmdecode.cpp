#include <dmtx.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// libdmtx decode from a preprocessed grayscale cv::Mat.
// The image is expected to be pre-binarised (0=black, 255=white).
// Internally inverts to match DmtxPack8bppK convention (0=white, 255=black)
// and upsamples if too small, so libdmtx's edge detector fires reliably.
// ---------------------------------------------------------------------------

static std::string dmtxTryDecode(const cv::Mat &gray, int timeout_ms) {
  // Upsample if too small — libdmtx edge detector needs ≥ 4px per module
  cv::Mat working = gray;
  if (working.cols < 200 || working.rows < 200) {
    double scale = 300.0 / std::max(working.cols, working.rows);
    cv::resize(working, working, {}, scale, scale, cv::INTER_LINEAR);
  }
  // Soften hard mathematical edges so libdmtx's scanner can model module
  // centers
  cv::GaussianBlur(working, working, cv::Size(5, 5), 0);

  // DmtxPack8bppK expects standard grayscale mapping: 0=black, 255=white
  cv::Mat inv = working;

  // Ensure contiguous memory
  if (!inv.isContinuous())
    inv = inv.clone();

  DmtxImage *img = dmtxImageCreate(inv.data, inv.cols, inv.rows, DmtxPack8bppK);
  if (!img)
    return {};
  dmtxImageSetProp(img, DmtxPropImageFlip, DmtxFlipY);

  DmtxDecode *dec = dmtxDecodeCreate(img, 1);
  if (!dec) {
    dmtxImageDestroy(&img);
    return {};
  }

  dmtxDecodeSetProp(dec, DmtxPropEdgeMin, 8);
  dmtxDecodeSetProp(dec, DmtxPropSymbolSize, DmtxSymbolShapeAuto);

  DmtxTime t = dmtxTimeAdd(dmtxTimeNow(), timeout_ms);
  std::string result;

  DmtxRegion *reg = dmtxRegionFindNext(dec, &t);
  if (reg) {
    DmtxMessage *msg = dmtxDecodeMatrixRegion(dec, reg, DmtxUndefined);
    if (msg) {
      result.assign(reinterpret_cast<char *>(msg->output), msg->outputIdx);
      dmtxMessageDestroy(&msg);
    }
    dmtxRegionDestroy(&reg);
  }

  dmtxDecodeDestroy(&dec);
  dmtxImageDestroy(&img);
  return result;
}

// Preprocessing variants — ordered cheapest-first

struct Variant {
  cv::Mat img;
  std::string label;
};

static std::vector<Variant> buildVariants(const cv::Mat &gray) {
  std::vector<Variant> v;

  // Pass 0: raw
  v.push_back({gray.clone(), "raw"});

  // Pass 0.5: Synthetic simulation (blur + downscale + upsample to soften)
  {
    cv::Mat soft;
    cv::GaussianBlur(gray, soft, cv::Size(9, 9), 2.0);
    v.push_back({soft, "synthetic-blur"});
  }

  // Pass 1: simple global Otsu threshold
  {
    cv::Mat t;
    cv::threshold(gray, t, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    v.push_back({t, "otsu"});
  }

  // Pass 2: CLAHE + adaptive threshold combos
  for (int inv : {0, 1})
    for (int block : {21, 31, 51})
      for (double cl : {2.0, 4.0}) {
        cv::Mat cl_img, blur, th;
        cv::createCLAHE(cl, cv::Size(8, 8))->apply(gray, cl_img);
        cv::GaussianBlur(cl_img, blur, cv::Size(5, 5), 0);
        cv::adaptiveThreshold(blur, th, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY, block, 10);
        if (inv)
          cv::bitwise_not(th, th);
        std::string lbl = "clahe=" + std::to_string((int)cl) +
                          " blk=" + std::to_string(block) + (inv ? " inv" : "");
        v.push_back({th, lbl});
      }
  return v;
}

// ROI candidates — look for square-ish contours that could be the symbol

static std::vector<cv::RotatedRect> findCandidates(const cv::Mat &binary) {
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  std::vector<cv::RotatedRect> out;
  for (auto &c : contours) {
    double area = cv::contourArea(c);
    if (area < 300)
      continue;
    cv::RotatedRect rr = cv::minAreaRect(c);
    float w = rr.size.width, h = rr.size.height;
    if (w < 1 || h < 1)
      continue;
    float aspect = (w > h) ? w / h : h / w;
    if (aspect < 5.0)
      out.push_back(rr); // reject very elongated shapes
  }
  return out;
}

// Perspective-correct a rotated rect from the source image (grayscale)
static cv::Mat warpROI(const cv::Mat &src, const cv::RotatedRect &rr,
                       int pad = 20) {
  float w = rr.size.width + pad * 2;
  float h = rr.size.height + pad * 2;
  float p = static_cast<float>(pad);

  cv::Point2f srcPts[4], dstPts[4];
  rr.points(srcPts);
  dstPts[0] = {p, h - p};
  dstPts[1] = {p, p};
  dstPts[2] = {w - p, p};
  dstPts[3] = {w - p, h - p};

  cv::Mat M = cv::getPerspectiveTransform(srcPts, dstPts);
  cv::Mat warped;
  cv::warpPerspective(src, warped, M, {(int)w, (int)h}, cv::INTER_LINEAR,
                      cv::BORDER_REPLICATE);
  return warped;
}

// Full decode pipeline

struct DecodeResult {
  std::string data;
  std::string pass;
  cv::Rect bbox;
};

static DecodeResult decodeImage(const cv::Mat &src, bool verbose) {
  cv::Mat gray;
  if (src.channels() == 1)
    gray = src;
  else
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

  auto variants = buildVariants(gray);

  for (auto &var : variants) {
    // 1. Try full-image decode
    std::string r = dmtxTryDecode(var.img, 300);
    if (!r.empty()) {
      if (verbose)
        std::cerr << "[pass] full-image | prep: " << var.label << "\n";
      return {r, var.label, {0, 0, src.cols, src.rows}};
    }

    // 2. ROI-based decode (skip raw and synthetic-blur — contours not
    // meaningful)
    if (var.label == "raw" || var.label == "synthetic-blur")
      continue;
    for (auto &rr : findCandidates(var.img)) {
      cv::Mat patch = warpROI(gray, rr); // warp grayscale

      // Threshold the warped patch cleanly
      cv::Mat pBin;
      cv::threshold(patch, pBin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

      r = dmtxTryDecode(pBin, 300);
      if (r.empty()) {
        cv::bitwise_not(pBin, pBin);
        r = dmtxTryDecode(pBin, 300);
      }
      if (!r.empty()) {
        cv::Rect bbox = rr.boundingRect();
        if (verbose)
          std::cerr << "[pass] ROI " << bbox << " | prep: " << var.label
                    << "\n";
        return {r, var.label + " +ROI", bbox};
      }
    }
  }
  return {};
}

// Main

static void usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <image>                  Decode single image\n"
            << "  " << prog << " --dir <dir> [--ext <exts>]  Batch decode\n"
            << "\n"
            << "  --verbose    Show which preprocessing pass succeeded\n"
            << "  --debug <f>  Save annotated image (single mode only)\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  std::string dirPath, debugOut, extFilter;
  std::vector<std::string> images;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char *f) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << f << " needs argument\n";
        std::exit(1);
      }
      return argv[++i];
    };
    if (a == "--verbose")
      verbose = true;
    else if (a == "--debug")
      debugOut = need("--debug");
    else if (a == "--dir")
      dirPath = need("--dir");
    else if (a == "--ext")
      extFilter = need("--ext");
    else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else
      images.push_back(a);
  }

  if (!dirPath.empty()) {
    for (auto &e : fs::directory_iterator(dirPath)) {
      if (!e.is_regular_file())
        continue;
      std::string fname = e.path().filename().string();
      if (fname.empty() || fname[0] == '.')
        continue;
      std::string ext = e.path().extension().string();
      if (!ext.empty() && ext[0] == '.')
        ext = ext.substr(1);
      for (auto &c : ext)
        c = std::tolower(c);
      if (extFilter.empty() || extFilter.find(ext) != std::string::npos)
        images.push_back(e.path().string());
    }
  }

  if (images.empty()) {
    std::cerr << "No input images.\n";
    usage(argv[0]);
    return 1;
  }

  int ok = 0, fail = 0;
  for (auto &path : images) {
    cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
    if (img.empty()) {
      std::cerr << "Cannot load: " << path << "\n";
      ++fail;
      continue;
    }

    DecodeResult res = decodeImage(img, verbose);

    if (!res.data.empty()) {
      std::cout << path << ": " << res.data << "\n";
      ++ok;

      if (!debugOut.empty() && images.size() == 1) {
        cv::rectangle(img, res.bbox, {0, 255, 0}, 2);
        cv::putText(img, res.data, {res.bbox.x, res.bbox.y - 8},
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 255, 0}, 2);
        cv::imwrite(debugOut, img);
        if (verbose)
          std::cerr << "Debug image: " << debugOut << "\n";
      }
    } else {
      std::cout << path << ": FAILED\n";
      ++fail;
    }
  }

  if (images.size() > 1)
    std::cerr << "\nDecoded: " << ok << "/" << (ok + fail) << "\n";

  return fail > 0 ? 1 : 0;
}
