// SPDX-License-Identifier: GPL-3.0-or-later
// Ported and adapted from scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)

#include <AdaptiveBinarize.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace cv;
using std::clamp;

namespace adaptive {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static Mat toGray(const Mat& src) {
    if (src.channels() == 1)
        return src.clone();
    Mat g;
    cvtColor(src, g, COLOR_BGR2GRAY);
    return g;
}

// Ensure windowSize is a positive odd number ≥ 3.
static int enforceOdd(int ws) {
    if (ws < 3) ws = 3;
    if (ws % 2 == 0) ws += 1;
    return ws;
}

// Build integral (sum) and squared-integral (sum-of-squares) from a gray image.
// Output types: integral → CV_64F, sqIntegral → CV_64F (via double).
static void buildIntegrals(const Mat& gray,
                           Mat& intImg,   // sum       [h+1 × w+1, double]
                           Mat& sqIntImg) // sq-sum    [h+1 × w+1, double]
{
    integral(gray, intImg, sqIntImg, CV_64F, CV_64F);
}

// Query sum and sq-sum inside [y0,y1) × [x0,x1) from pre-built integral images.
static inline void windowStats(const Mat& intImg, const Mat& sqIntImg,
                                int y0, int y1, int x0, int x1,
                                double& outMean, double& outStd)
{
    // The standard 2-D prefix-sum formula:
    //   sum(R) = I[y1][x1] - I[y0][x1] - I[y1][x0] + I[y0][x0]
    double sum   = intImg .at<double>(y1,x1) - intImg .at<double>(y0,x1)
                 - intImg .at<double>(y1,x0) + intImg .at<double>(y0,x0);
    double sqSum = sqIntImg.at<double>(y1,x1) - sqIntImg.at<double>(y0,x1)
                 - sqIntImg.at<double>(y1,x0) + sqIntImg.at<double>(y0,x0);

    int area  = (y1 - y0) * (x1 - x0);
    double rA = 1.0 / area;
    outMean   = sum * rA;
    double var = sqSum * rA - outMean * outMean;
    outStd    = (var > 0.0) ? std::sqrt(var) : 0.0;
}

// ---------------------------------------------------------------------------
// Sauvola
// ---------------------------------------------------------------------------

/**
 * Formula (modified by zvezdochiot):
 *   threshold = mean × (1 − k × (1 − S/128 − delta/128))
 */
void binarizeSauvola(const Mat& src, Mat& dst,
                     int windowSize, double k, double delta)
{
    windowSize = enforceOdd(windowSize);
    Mat gray   = toGray(src);
    const int W = gray.cols, H = gray.rows;

    Mat intImg, sqIntImg;
    buildIntegrals(gray, intImg, sqIntImg);

    const int half = windowSize / 2;
    const double fracD = delta / 128.0;

    dst.create(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* gRow = gray.ptr<uchar>(y);
        uchar*       dRow = dst .ptr<uchar>(y);
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(H, y + half + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(W, x + half + 1);
            double mean, stdv;
            windowStats(intImg, sqIntImg, y0, y1, x0, x1, mean, stdv);
            double fracS = stdv / 128.0;
            double thr   = mean * (1.0 - k * (1.0 - (fracS + fracD)));
            dRow[x] = (gRow[x] < thr) ? 0 : 255;
        }
    }
}

// ---------------------------------------------------------------------------
// Wolf
// ---------------------------------------------------------------------------

/**
 * Formula:
 *   threshold = (1−k)×M + k×minGlobal + k×(S/maxS)×(M − minGlobal)
 */
void binarizeWolf(const Mat& src, Mat& dst,
                  int windowSize, double k, double delta)
{
    windowSize = enforceOdd(windowSize);
    Mat gray   = toGray(src);
    const int W = gray.cols, H = gray.rows;

    double minGray = 255.0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            minGray = std::min(minGray, (double)gray.at<uchar>(y, x));

    Mat intImg, sqIntImg;
    buildIntegrals(gray, intImg, sqIntImg);

    const int half = windowSize / 2;

    // First pass: compute per-pixel mean, std and track maxStd
    std::vector<float> means(W * H), stds(W * H);
    double maxStd = 1e-12;
    for (int y = 0; y < H; ++y) {
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(H, y + half + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(W, x + half + 1);
            double mean, stdv;
            windowStats(intImg, sqIntImg, y0, y1, x0, x1, mean, stdv);
            means[y * W + x] = (float)mean;
            stds [y * W + x] = (float)stdv;
            maxStd = std::max(maxStd, stdv);
        }
    }

    const double fracD = delta / 128.0;
    dst.create(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* gRow = gray.ptr<uchar>(y);
        uchar*       dRow = dst .ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            double mean  = means[y * W + x];
            double stdv  = stds [y * W + x];
            double base  = mean - minGray;
            double fracN = stdv / maxStd;
            double thr   = base * (1.0 - k * (1.0 - (fracN + fracD))) + minGray;
            dRow[x] = (gRow[x] < thr) ? 0 : 255;
        }
    }
}

// ---------------------------------------------------------------------------
// Bradley
// ---------------------------------------------------------------------------

/**
 * Formula:  threshold = mean × (1 − k) + delta/2
 */
void binarizeBradley(const Mat& src, Mat& dst,
                     int windowSize, double k, double delta)
{
    windowSize = enforceOdd(windowSize);
    Mat gray   = toGray(src);
    const int W = gray.cols, H = gray.rows;

    Mat intImg, sqIntImg;
    buildIntegrals(gray, intImg, sqIntImg);

    const int half = windowSize / 2;
    const double offset = delta / 2.0;

    dst.create(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* gRow = gray.ptr<uchar>(y);
        uchar*       dRow = dst .ptr<uchar>(y);
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(H, y + half + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(W, x + half + 1);
            double mean, stdv;
            windowStats(intImg, sqIntImg, y0, y1, x0, x1, mean, stdv);
            double thr = mean * (1.0 - k) + offset;
            dRow[x] = (gRow[x] < thr) ? 0 : 255;
        }
    }
}

// ---------------------------------------------------------------------------
// EdgeDiv — zvezdochiot 2023
// ---------------------------------------------------------------------------

void binarizeEdgeDiv(const Mat& src, Mat& dst,
                     int windowSize, double kep, double kdb, double delta)
{
    windowSize = enforceOdd(windowSize);
    Mat gray   = toGray(src);
    const int W = gray.cols, H = gray.rows;

    Mat intImg, sqIntImg;
    buildIntegrals(gray, intImg, sqIntImg);

    const int    half    = windowSize / 2;
    const double kTotal  = kep + kdb;
    if (kTotal < 1e-9) {
        dst = Mat(H, W, CV_8UC1, Scalar(255));
        return;
    }

    Mat blended(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* gRow = gray.ptr<uchar>(y);
        uchar*       bRow = blended.ptr<uchar>(y);
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(H, y + half + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(W, x + half + 1);
            double mean, stdv;
            windowStats(intImg, sqIntImg, y0, y1, x0, x1, mean, stdv);

            double pixel = gRow[x];
            // EdgePlus: clamp(pixel + mean − blur, 0, 255)   here blur ≈ mean
            double ep = clamp(pixel + mean - mean, 0.0, 255.0); // simplifies to pixel
            // BlurDiv : mean>0 ? clamp(pixel*256/(mean+1), 0, 255) : pixel
            double bd = (mean > 0.0)
                      ? clamp(pixel * 256.0 / (mean + 1.0), 0.0, 255.0)
                      : pixel;
            bRow[x] = (uchar)clamp((kep * ep + kdb * bd) / kTotal, 0.0, 255.0);
        }
    }
    // Global Otsu on the blended image, shifted by delta
    double otsuThr = threshold(blended, dst, 0, 255, THRESH_BINARY | THRESH_OTSU);
    // If delta requested, re-apply with shifted threshold
    if (std::abs(delta) > 1e-9) {
        double newThr = otsuThr + delta;
        threshold(blended, dst, newThr, 255, THRESH_BINARY);
    }
}

// ---------------------------------------------------------------------------
// Grad — zvezdochiot 2024
// ---------------------------------------------------------------------------

/**
 * A pixel is black when:  pixel < M − k × (maxStd − S)
 */
void binarizeGrad(const Mat& src, Mat& dst,
                  int windowSize, double k, double delta)
{
    windowSize = enforceOdd(windowSize);
    Mat gray   = toGray(src);
    const int W = gray.cols, H = gray.rows;

    Mat intImg, sqIntImg;
    buildIntegrals(gray, intImg, sqIntImg);

    const int half = windowSize / 2;

    std::vector<float> means(W * H), stds(W * H);
    double maxStd = 1e-12;
    for (int y = 0; y < H; ++y) {
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(H, y + half + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(W, x + half + 1);
            double mean, stdv;
            windowStats(intImg, sqIntImg, y0, y1, x0, x1, mean, stdv);
            means[y * W + x] = (float)mean;
            stds [y * W + x] = (float)stdv;
            maxStd = std::max(maxStd, stdv);
        }
    }

    dst.create(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* gRow = gray.ptr<uchar>(y);
        uchar*       dRow = dst .ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            double mean = means[y * W + x];
            double stdv = stds [y * W + x];
            double thr  = mean - k * (maxStd - stdv) + delta;
            dRow[x] = (gRow[x] < thr) ? 0 : 255;
        }
    }
}

} // namespace adaptive
