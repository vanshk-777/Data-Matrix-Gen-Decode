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

    for (int shrink : {1, 2, 4, 8}) {
        DmtxImage* img = dmtxImageCreate(gray.data, gray.cols, gray.rows, DmtxPack8bppK);
        dmtxImageSetProp(img, DmtxPropImageFlip, DmtxFlipY);
        DmtxDecode* dec = dmtxDecodeCreate(img, shrink);
        DmtxTime t = dmtxTimeAdd(dmtxTimeNow(), 2000);
        DmtxRegion* reg = dmtxRegionFindNext(dec, &t);
        if (reg) {
            DmtxMessage* msg = dmtxDecodeMatrixRegion(dec, reg, DmtxUndefined);
            if (msg) {
                std::cout << "[Shrink " << shrink << "] OK: " << msg->output << "\n";
                dmtxMessageDestroy(&msg);
                return 0;
            }
            std::cout << "[Shrink " << shrink << "] Msg Null\n";
        } else {
            std::cout << "[Shrink " << shrink << "] Reg Null\n";
        }
    }
    return 0;
}
