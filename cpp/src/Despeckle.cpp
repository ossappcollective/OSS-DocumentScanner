// SPDX-License-Identifier: GPL-3.0-or-later
// Ported and adapted from scantailor-advanced Despeckle
// (https://github.com/farfromrefug/scantailor-advanced)

#include <Despeckle.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>

using namespace cv;

namespace speckle {

static int areaThreshold(DespeckleLevel level) {
    switch (level) {
        case DespeckleLevel::CAUTIOUS:   return 5;
        case DespeckleLevel::NORMAL:     return 20;
        case DespeckleLevel::AGGRESSIVE: return 100;
    }
    return 20;
}

static Mat buildRemovalMask(const Mat& src, DespeckleLevel level) {
    // Convert to 8-bit grayscale
    Mat gray;
    if (src.channels() > 1)
        cvtColor(src, gray, COLOR_BGR2GRAY);
    else
        gray = src.clone();
    if (gray.depth() != CV_8U)
        gray.convertTo(gray, CV_8U);

    // Binarise: foreground = black (0), background = white (255)
    Mat binary;
    threshold(gray, binary, 0, 255, THRESH_BINARY | THRESH_OTSU);
    // Invert so foreground components are labeled
    Mat fgMask;
    bitwise_not(binary, fgMask);

    // Connected components
    Mat labels, stats, centroids;
    int n = connectedComponentsWithStats(fgMask, labels, stats, centroids, 8, CV_32S);

    const int areaMin = areaThreshold(level);

    // Build removal mask: pixels belonging to small components → white (erase)
    Mat removeMask(src.rows, src.cols, CV_8UC1, Scalar(0));
    for (int i = 1; i < n; ++i) { // skip label 0 (background)
        int area = stats.at<int>(i, CC_STAT_AREA);
        if (area < areaMin) {
            // Mark all pixels of this component for removal
            Mat compMask = (labels == i);
            removeMask.setTo(255, compMask);
        }
    }
    return removeMask;
}

void despeckle(const Mat& src, Mat& dst, DespeckleLevel level) {
    if (src.empty()) { dst = src.clone(); return; }
    dst = src.clone();
    Mat mask = buildRemovalMask(src, level);
    // Paint removed pixels white
    dst.setTo(Scalar::all(255), mask);
}

void despeckleInPlace(Mat& img, DespeckleLevel level) {
    if (img.empty()) return;
    Mat mask = buildRemovalMask(img, level);
    img.setTo(Scalar::all(255), mask);
}

} // namespace speckle
