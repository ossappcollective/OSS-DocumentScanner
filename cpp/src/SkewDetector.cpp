// SPDX-License-Identifier: GPL-3.0-or-later
// Approach inspired by SkewFinder in scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)

#include <SkewDetector.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>
#include <limits>

using namespace cv;

namespace skew {

static Mat toGray(const Mat& src) {
    if (src.channels() == 1) return src.clone();
    Mat g;
    cvtColor(src, g, COLOR_BGR2GRAY);
    return g;
}

// Compute variance of a projection profile (row sums) of a binary image.
static double projectionVariance(const Mat& binary) {
    const int H = binary.rows;
    const int W = binary.cols;
    double sum = 0.0, sq = 0.0;
    for (int y = 0; y < H; ++y) {
        int rowSum = 0;
        const uchar* row = binary.ptr<uchar>(y);
        for (int x = 0; x < W; ++x)
            rowSum += (row[x] == 0) ? 1 : 0; // count black pixels
        sum += rowSum;
        sq  += (double)rowSum * rowSum;
    }
    double mean = sum / H;
    return sq / H - mean * mean;
}

SkewResult detectSkew(const Mat& src, double maxAngleDeg) {
    Mat gray  = toGray(src);
    Mat binary;
    threshold(gray, binary, 0, 255, THRESH_BINARY | THRESH_OTSU);

    // Down-scale for speed if large
    Mat work = binary;
    if (work.cols > 800) {
        double sc = 800.0 / work.cols;
        resize(work, work, Size(), sc, sc, INTER_NEAREST);
    }

    const Point2f center(work.cols / 2.0f, work.rows / 2.0f);
    const double step = 0.5; // degrees
    double bestAngle = 0.0;
    double bestVar   = -1.0;
    double worstVar  = std::numeric_limits<double>::max();

    for (double a = -maxAngleDeg; a <= maxAngleDeg; a += step) {
        Mat rot   = getRotationMatrix2D(center, a, 1.0);
        Mat rotImg;
        warpAffine(work, rotImg, rot, work.size(),
                   INTER_NEAREST, BORDER_CONSTANT, Scalar(255));
        double v = projectionVariance(rotImg);
        if (v > bestVar) { bestVar = v; bestAngle = a; }
        if (v < worstVar)  worstVar = v;
    }

    double confidence = (worstVar > 1e-9) ? (bestVar / worstVar) : 1.0;
    return { bestAngle, confidence };
}

Mat correctSkew(const Mat& src, double angleDeg) {
    const Point2f center(src.cols / 2.0f, src.rows / 2.0f);
    Mat rot = getRotationMatrix2D(center, angleDeg, 1.0);
    Mat dst;
    warpAffine(src, dst, rot, src.size(),
               INTER_LINEAR, BORDER_CONSTANT, Scalar(255, 255, 255));
    return dst;
}

} // namespace skew
