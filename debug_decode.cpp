#include <dmtx.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    cv::Mat bgr = cv::imread(argv[1], cv::IMREAD_COLOR);
    if(bgr.empty()) return 1;

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    // Test 1: standard gray (0=black, 255=white)
    {
        DmtxImage* img = dmtxImageCreate(gray.data, gray.cols, gray.rows, DmtxPack8bppK);
        dmtxImageSetProp(img, DmtxPropImageFlip, DmtxFlipY);
        DmtxDecode* dec = dmtxDecodeCreate(img, 1);
        DmtxTime t = dmtxTimeAdd(dmtxTimeNow(), 1000);
        DmtxRegion* reg = dmtxRegionFindNext(dec, &t);
        if (reg) {
            DmtxMessage* msg = dmtxDecodeMatrixRegion(dec, reg, DmtxUndefined);
            if (msg) std::cout << "[Test 1: Standard Grayscale] OK: " << msg->output << "\n";
            else std::cout << "[Test 1] Msg Null\n";
        } else std::cout << "[Test 1] Reg Null\n";
    }

    // Test 2: inverted gray (255=black, 0=white)
    {
        cv::Mat inv;
        cv::bitwise_not(gray, inv);
        DmtxImage* img = dmtxImageCreate(inv.data, inv.cols, inv.rows, DmtxPack8bppK);
        dmtxImageSetProp(img, DmtxPropImageFlip, DmtxFlipY);
        DmtxDecode* dec = dmtxDecodeCreate(img, 1);
        DmtxTime t = dmtxTimeAdd(dmtxTimeNow(), 1000);
        DmtxRegion* reg = dmtxRegionFindNext(dec, &t);
        if (reg) {
            DmtxMessage* msg = dmtxDecodeMatrixRegion(dec, reg, DmtxUndefined);
            if (msg) std::cout << "[Test 2: Inverted Grayscale] OK: " << msg->output << "\n";
            else std::cout << "[Test 2] Msg Null\n";
        } else std::cout << "[Test 2] Reg Null\n";
    }
    return 0;
}
