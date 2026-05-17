// SPDX-License-Identifier: GPL-3.0-or-later
// Inspired by scantailor-advanced EstimateBackground / PolynomialSurface
// (https://github.com/farfromrefug/scantailor-advanced)

#include <BackgroundEstimator.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>
#include <algorithm>

using namespace cv;

namespace bgest {

// Convert to 8-bit grayscale
static Mat toGray8(const Mat& src) {
    Mat g;
    if (src.channels() > 1)
        cvtColor(src, g, COLOR_BGR2GRAY);
    else
        g = src.clone();
    if (g.depth() != CV_8U)
        g.convertTo(g, CV_8U);
    return g;
}

// Build the polynomial feature vector for a point (nx, ny) in [-1,1]^2.
// degree = 1 → [1, nx, ny]
// degree = 2 → [1, nx, ny, nx^2, nx*ny, ny^2]  etc.
static std::vector<double> polyFeatures(double nx, double ny, int degree) {
    std::vector<double> feats;
    feats.reserve((degree + 1) * (degree + 2) / 2);
    for (int d = 0; d <= degree; ++d) {
        for (int j = 0; j <= d; ++j) {
            int px = d - j, py = j;
            double val = 1.0;
            for (int k = 0; k < px; ++k) val *= nx;
            for (int k = 0; k < py; ++k) val *= ny;
            feats.push_back(val);
        }
    }
    return feats;
}

cv::Mat estimateBackground(const Mat& src, int polyDegree) {
    if (src.empty()) return {};
    polyDegree = std::clamp(polyDegree, 1, 8);

    Mat gray = toGray8(src);
    const int W = gray.cols, H = gray.rows;

    const float mf = 0.15f; // margin fraction
    int marginX = std::max(1, (int)(W * mf));
    int marginY = std::max(1, (int)(H * mf));

    // Collect margin samples
    std::vector<std::pair<Point, uchar>> samples;
    for (int y = 0; y < H; ++y) {
        const uchar* row = gray.ptr<uchar>(y);
        bool yInMargin = (y < marginY || y >= H - marginY);
        for (int x = 0; x < W; ++x) {
            if (yInMargin || x < marginX || x >= W - marginX)
                samples.push_back({Point(x, y), row[x]});
        }
    }
    if (samples.empty()) {
        return Mat(H, W, CV_8UC1, Scalar(128));
    }

    int nPoly = (polyDegree + 1) * (polyDegree + 2) / 2;
    int N     = (int)samples.size();

    Mat A(N, nPoly, CV_64F);
    Mat b(N, 1,     CV_64F);

    for (int i = 0; i < N; ++i) {
        double nx = (samples[i].first.x * 2.0 / (W - 1)) - 1.0;
        double ny = (samples[i].first.y * 2.0 / (H - 1)) - 1.0;
        auto feats = polyFeatures(nx, ny, polyDegree);
        for (int j = 0; j < nPoly; ++j)
            A.at<double>(i, j) = feats[j];
        b.at<double>(i, 0) = samples[i].second;
    }

    Mat coeffs;
    solve(A, b, coeffs, DECOMP_SVD);

    // Reconstruct background image
    Mat bg(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        uchar* row = bg.ptr<uchar>(y);
        double ny  = (y * 2.0 / (H - 1)) - 1.0;
        for (int x = 0; x < W; ++x) {
            double nx   = (x * 2.0 / (W - 1)) - 1.0;
            auto feats  = polyFeatures(nx, ny, polyDegree);
            double val  = 0.0;
            for (int j = 0; j < nPoly; ++j)
                val += feats[j] * coeffs.at<double>(j, 0);
            row[x] = (uchar)std::clamp(val, 0.0, 255.0);
        }
    }
    return bg;
}

void normalizeIllumination(const Mat& src, Mat& dst, int polyDegree) {
    if (src.empty()) { dst = src.clone(); return; }
    Mat bg = estimateBackground(src, polyDegree);
    if (bg.empty()) { dst = src.clone(); return; }

    const int W = src.cols, H = src.rows;
    const int ch = src.channels();

    dst = src.clone();

    for (int y = 0; y < H; ++y) {
        const uchar* bgRow = bg  .ptr<uchar>(y);
        const uchar* sRow  = src .ptr<uchar>(y);
        uchar*       dRow  = dst .ptr<uchar>(y);

        for (int x = 0; x < W; ++x) {
            double bgVal = std::max((double)bgRow[x], 1.0);
            double scale = 255.0 / bgVal;

            for (int c = 0; c < ch; ++c) {
                double val = sRow[x * ch + c] * scale;
                dRow[x * ch + c] = (uchar)std::clamp(val, 0.0, 255.0);
            }
        }
    }
}

} // namespace bgest
