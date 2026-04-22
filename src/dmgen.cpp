#include <dmtx.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Symbol size table

struct SizeEntry {
  const char *name;
  DmtxSymbolSize val;
};

static const SizeEntry SIZE_TABLE[] = {
    {"auto", DmtxSymbolShapeAuto},  {"10x10", DmtxSymbol10x10},
    {"12x12", DmtxSymbol12x12},     {"14x14", DmtxSymbol14x14},
    {"16x16", DmtxSymbol16x16},     {"18x18", DmtxSymbol18x18},
    {"20x20", DmtxSymbol20x20},     {"22x22", DmtxSymbol22x22},
    {"24x24", DmtxSymbol24x24},     {"26x26", DmtxSymbol26x26},
    {"32x32", DmtxSymbol32x32},     {"36x36", DmtxSymbol36x36},
    {"40x40", DmtxSymbol40x40},     {"44x44", DmtxSymbol44x44},
    {"48x48", DmtxSymbol48x48},     {"52x52", DmtxSymbol52x52},
    {"64x64", DmtxSymbol64x64},     {"72x72", DmtxSymbol72x72},
    {"80x80", DmtxSymbol80x80},     {"88x88", DmtxSymbol88x88},
    {"96x96", DmtxSymbol96x96},     {"104x104", DmtxSymbol104x104},
    {"120x120", DmtxSymbol120x120}, {"132x132", DmtxSymbol132x132},
    {"144x144", DmtxSymbol144x144}, {"8x18", DmtxSymbol8x18},
    {"8x32", DmtxSymbol8x32},       {"12x26", DmtxSymbol12x26},
    {"12x36", DmtxSymbol12x36},     {"16x36", DmtxSymbol16x36},
    {"16x48", DmtxSymbol16x48},
};

static DmtxSymbolSize parseSize(const std::string &s) {
  for (auto &e : SIZE_TABLE)
    if (s == e.name)
      return e.val;
  throw std::invalid_argument("Unknown size '" + s +
                              "'. Run --sizes to list options.");
}

static void printSizes() {
  std::cout << "Valid -s values:\n";
  for (auto &e : SIZE_TABLE)
    std::cout << "  " << e.name << "\n";
}

// Encoding

struct Grid {
  int rows, cols;
  std::vector<bool> cells;
};

static Grid encode(const std::string &data, int moduleSize,
                   DmtxSymbolSize symSize) {
  DmtxEncode *enc = dmtxEncodeCreate();
  if (!enc)
    throw std::runtime_error("dmtxEncodeCreate() failed");

  dmtxEncodeSetProp(enc, DmtxPropModuleSize, moduleSize);
  dmtxEncodeSetProp(enc, DmtxPropMarginSize, moduleSize * 2);
  dmtxEncodeSetProp(enc, DmtxPropSizeRequest, symSize);

  if (dmtxEncodeDataMatrix(enc, static_cast<int>(data.size()),
                           reinterpret_cast<unsigned char *>(
                               const_cast<char *>(data.data()))) == DmtxFail) {
    dmtxEncodeDestroy(&enc);
    throw std::runtime_error("Encode failed: " + data);
  }

  DmtxImage *img = enc->image;
  int W = dmtxImageGetProp(img, DmtxPropWidth);
  int H = dmtxImageGetProp(img, DmtxPropHeight);
  int bpp = dmtxImageGetProp(img, DmtxPropBytesPerPixel);
  int margin = moduleSize * 2;
  int symW = (W - 2 * margin) / moduleSize;
  int symH = (H - 2 * margin) / moduleSize;

  Grid g{symH, symW, std::vector<bool>(symH * symW)};

  for (int r = 0; r < symH; ++r) {
    for (int c = 0; c < symW; ++c) {
      int px = margin + c * moduleSize + moduleSize / 2;
      int py =
          H - 1 -
          (margin + r * moduleSize + moduleSize / 2); // libdmtx is bottom-up
      unsigned char pval = img->pxl[py * W * bpp + px * bpp];
      g.cells[r * symW + c] = (pval < 128);
    }
  }

  dmtxEncodeDestroy(&enc);
  return g;
}

// Output

static void writeSVG(const Grid &g, const std::string &path, int moduleSize,
                     const std::string &data) {
  int qz = moduleSize * 2;
  int svgW = g.cols * moduleSize + 2 * qz;
  int svgH = g.rows * moduleSize + 2 * qz;

  std::ofstream f(path);
  if (!f)
    throw std::runtime_error("Cannot open: " + path);

  f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    << "<svg xmlns=\"http://www.w3.org/2000/svg\""
    << " width=\"" << svgW << "\" height=\"" << svgH << "\""
    << " viewBox=\"0 0 " << svgW << " " << svgH << "\">\n"
    << "  <!-- " << data << " -->\n"
    << "  <rect width=\"" << svgW << "\" height=\"" << svgH
    << "\" fill=\"white\"/>\n";

  for (int r = 0; r < g.rows; ++r)
    for (int c = 0; c < g.cols; ++c)
      if (g.cells[r * g.cols + c])
        f << "  <rect x=\"" << (qz + c * moduleSize) << "\" y=\""
          << (qz + r * moduleSize) << "\" width=\"" << moduleSize
          << "\" height=\"" << moduleSize << "\" fill=\"black\"/>\n";

  f << "</svg>\n";
}

static void writePNG(const Grid &g, const std::string &path, int moduleSize) {
  int qz = moduleSize * 2;
  int imgW = g.cols * moduleSize + 2 * qz;
  int imgH = g.rows * moduleSize + 2 * qz;

  std::vector<unsigned char> pixels(imgW * imgH, 255); // white background

  for (int r = 0; r < g.rows; ++r)
    for (int c = 0; c < g.cols; ++c)
      if (g.cells[r * g.cols + c])
        for (int dy = 0; dy < moduleSize; ++dy)
          for (int dx = 0; dx < moduleSize; ++dx)
            pixels[(qz + r * moduleSize + dy) * imgW +
                   (qz + c * moduleSize + dx)] = 0;

  if (!stbi_write_png(path.c_str(), imgW, imgH, 1, pixels.data(), imgW))
    throw std::runtime_error("stbi_write_png failed: " + path);
}

static void writeOutput(const Grid &g, const std::string &outPath,
                        int moduleSize, const std::string &data) {
  fs::create_directories(fs::path(outPath).parent_path());
  std::string ext;
  auto dot = outPath.rfind('.');
  if (dot != std::string::npos) {
    ext = outPath.substr(dot + 1);
    for (auto &ch : ext)
      ch = std::tolower(ch);
  }

  if (ext == "png")
    writePNG(g, outPath, moduleSize);
  else
    writeSVG(g, ext == "svg" ? outPath : outPath + ".svg", moduleSize, data);
}

// Parsers (JSON + CSV)

static std::string jsonUnescape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\') {
      out += s[i];
      continue;
    }
    if (++i >= s.size())
      break;
    switch (s[i]) {
    case '"':
      out += '"';
      break;
    case '\\':
      out += '\\';
      break;
    case 'n':
      out += '\n';
      break;
    case 't':
      out += '\t';
      break;
    default:
      out += s[i];
      break;
    }
  }
  return out;
}

static std::string readJsonString(const std::string &src, size_t &pos) {
  std::string val;
  while (pos < src.size()) {
    char c = src[pos++];
    if (c == '"')
      return jsonUnescape(val);
    if (c == '\\') {
      val += c;
      if (pos < src.size())
        val += src[pos++];
    } else
      val += c;
  }
  throw std::runtime_error("Unterminated JSON string");
}

struct Label {
  std::string data, filename;
};

static std::vector<Label> parseJSON(const std::string &src) {
  std::vector<Label> labels;
  size_t pos = 0;
  auto skip = [&]() {
    while (pos < src.size() && std::isspace((unsigned char)src[pos]))
      ++pos;
  };

  skip();
  if (pos >= src.size() || src[pos] != '[')
    throw std::runtime_error("JSON must be an array");
  ++pos;

  while (true) {
    skip();
    if (pos >= src.size())
      throw std::runtime_error("Unexpected end of JSON");
    char c = src[pos];
    if (c == ']') {
      ++pos;
      break;
    }
    if (c == ',') {
      ++pos;
      continue;
    }

    Label lbl;
    if (c == '"') {
      ++pos;
      lbl.data = readJsonString(src, pos);
    } else if (c == '{') {
      ++pos;
      while (true) {
        skip();
        if (pos >= src.size())
          throw std::runtime_error("Unterminated JSON object");
        char oc = src[pos];
        if (oc == '}') {
          ++pos;
          break;
        }
        if (oc == ',') {
          ++pos;
          continue;
        }
        if (oc == '"') {
          ++pos;
          std::string key = readJsonString(src, pos);
          skip();
          if (pos < src.size() && src[pos] == ':')
            ++pos;
          skip();
          if (pos < src.size() && src[pos] == '"') {
            ++pos;
            std::string val = readJsonString(src, pos);
            if (key == "data")
              lbl.data = val;
            if (key == "filename")
              lbl.filename = val;
          }
        } else {
          ++pos;
        }
      }
    } else {
      ++pos;
      continue;
    }

    if (!lbl.data.empty())
      labels.push_back(std::move(lbl));
  }
  return labels;
}

static std::vector<Label> parseCSV(const std::string &src) {
  std::vector<Label> labels;
  std::istringstream ss(src);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    Label lbl;
    auto comma = line.find(',');
    if (comma == std::string::npos)
      lbl.data = line;
    else {
      lbl.data = line.substr(0, comma);
      lbl.filename = line.substr(comma + 1);
    }
    for (auto *f : {&lbl.data, &lbl.filename})
      while (!f->empty() && (f->back() == '\r' || f->back() == ' '))
        f->pop_back();
    if (!lbl.data.empty())
      labels.push_back(std::move(lbl));
  }
  return labels;
}

static std::string sanitize(const std::string &s) {
  std::string out;
  for (char c : s)
    out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  return out.empty() ? "label" : out;
}

static void usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog
            << " -d <data> [-o <out.svg|png>] [-m <px>] [-s <NxN>]\n"
            << "  " << prog
            << " --json <file> [--out-dir <dir>] [-m <px>] [-s <NxN>] [--fmt "
               "svg|png]\n"
            << "  " << prog
            << " --csv  <file> [--out-dir <dir>] [-m <px>] [-s <NxN>] [--fmt "
               "svg|png]\n"
            << "\n"
            << "  -m <px>        Module size in pixels (default: 5)\n"
            << "  --max-dim <px> Maximum image dimension. Dynamically overrides -m to fit.\n"
            << "  -s <NxN>       Symbol size (default: auto). See --sizes.\n"
            << "  --fmt svg|png  Batch output format (default: svg)\n"
            << "  --sizes        List all valid symbol sizes\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  std::string singleData, outFile, jsonFile, csvFile, outDir = "out",
                                                      batchFmt = "svg";
  int moduleSize = 5;
  int maxDim = 0;
  DmtxSymbolSize symSize = DmtxSymbolShapeAuto;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (a == "--sizes") {
      printSizes();
      return 0;
    }

    auto need = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << flag << " requires an argument\n";
        std::exit(1);
      }
      return argv[++i];
    };

    if (a == "-d")
      singleData = need("-d");
    else if (a == "-o")
      outFile = need("-o");
    else if (a == "--json")
      jsonFile = need("--json");
    else if (a == "--csv")
      csvFile = need("--csv");
    else if (a == "--out-dir")
      outDir = need("--out-dir");
    else if (a == "--fmt")
      batchFmt = need("--fmt");
    else if (a == "-m")
      moduleSize = std::stoi(need("-m"));
    else if (a == "--max-dim")
      maxDim = std::stoi(need("--max-dim"));
    else if (a == "-s")
      symSize = parseSize(need("-s"));
    else {
      std::cerr << "Unknown option: " << a << "\n";
      usage(argv[0]);
      return 1;
    }
  }

  if (moduleSize < 1 || moduleSize > 100) {
    std::cerr << "Module size must be 1–100\n";
    return 1;
  }

  try {
    if (!singleData.empty()) {
      Grid g = encode(singleData, moduleSize, symSize);
      int finalModule = moduleSize;
      if (maxDim > 0) {
        int maxCells = std::max(g.cols, g.rows) + 4; // 2 * 2 quiet zone
        finalModule = maxDim / maxCells;
        if (finalModule < 1) finalModule = 1;
      }
      std::string out =
          outFile.empty() ? sanitize(singleData) + ".svg" : outFile;
      writeOutput(g, out, finalModule, singleData);
      std::cout << "Written: " << out << "  (" << g.cols << "x" << g.rows
                << " modules) @ " << finalModule << "px/mod\n";
      return 0;
    }

    std::vector<Label> labels;
    if (!jsonFile.empty()) {
      std::ifstream f(jsonFile);
      if (!f) {
        std::cerr << "Cannot open: " << jsonFile << "\n";
        return 1;
      }
      labels = parseJSON(std::string(std::istreambuf_iterator<char>(f), {}));
    } else if (!csvFile.empty()) {
      std::ifstream f(csvFile);
      if (!f) {
        std::cerr << "Cannot open: " << csvFile << "\n";
        return 1;
      }
      labels = parseCSV(std::string(std::istreambuf_iterator<char>(f), {}));
    } else {
      std::cerr << "No input. Use -d, --json, or --csv.\n";
      usage(argv[0]);
      return 1;
    }

    if (labels.empty()) {
      std::cerr << "No labels found.\n";
      return 1;
    }

    fs::create_directories(outDir);
    int ok = 0, fail = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
      const auto &lbl = labels[i];
      std::string stem =
          lbl.filename.empty() ? sanitize(lbl.data) : sanitize(lbl.filename);
      std::string outPath = outDir + "/" + stem + "." + batchFmt;
      try {
        Grid g = encode(lbl.data, moduleSize, symSize);
        int finalModule = moduleSize;
        if (maxDim > 0) {
          int maxCells = std::max(g.cols, g.rows) + 4;
          finalModule = maxDim / maxCells;
          if (finalModule < 1) finalModule = 1;
        }
        writeOutput(g, outPath, finalModule, lbl.data);
        std::cout << "[" << (i + 1) << "/" << labels.size() << "] " << outPath
                  << "  (" << g.cols << "x" << g.rows << " @ " << finalModule << "px/mod)\n";
        ++ok;
      } catch (const std::exception &e) {
        std::cerr << "  ERROR '" << lbl.data << "': " << e.what() << "\n";
        ++fail;
      }
    }

    std::cout << "\nDone: " << ok << " generated";
    if (fail)
      std::cout << ", " << fail << " failed";
    std::cout << "  →  " << outDir << "/\n";

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
