// SPDX-License-Identifier: GPL-3.0-or-later
// Ported and adapted from scantailor-advanced WienerFilter
// (https://github.com/farfromrefug/scantailor-advanced)

#include <WienerDenoiser.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <algorithm>
#include <stdexcept>

using namespace cv;

namespace denoiser {

static Mat toGray(const Mat& src) {
    if (src.channels() == 1) return src;
    Mat g;
    cvtColor(src, g, COLOR_BGR2GRAY);
    return g;
}

void wienerDenoise(const Mat& src, Mat& dst,
                   Size windowSize, double noiseSigma)
{
    if (src.empty()) { dst = src.clone(); return; }
    if (windowSize.width < 1 || windowSize.height < 1)
        throw std::invalid_argument("wienerDenoise: windowSize must be >= 1");

    Mat gray = toGray(src);
    if (gray.depth() != CV_8U) gray.convertTo(gray, CV_8U);

    const int W = gray.cols, H = gray.rows;
    const double noiseVar = noiseSigma * noiseSigma;

    // Build integral and squared-integral (CV_64F for accuracy)
    Mat intImg, sqIntImg;
    integral(gray, intImg, sqIntImg, CV_64F, CV_64F);

    const int halfH = windowSize.height / 2;
    const int halfW = windowSize.width  / 2;

    dst.create(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const uchar* sRow = gray.ptr<uchar>(y);
        uchar*       dRow = dst .ptr<uchar>(y);

        const int y0 = std::max(0, y - halfH);
        const int y1 = std::min(H, y + halfH + 1);

        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - halfW);
            const int x1 = std::min(W, x + halfW + 1);

            double sum   = intImg .at<double>(y1,x1) - intImg .at<double>(y0,x1)
                         - intImg .at<double>(y1,x0) + intImg .at<double>(y0,x0);
            double sqSum = sqIntImg.at<double>(y1,x1) - sqIntImg.at<double>(y0,x1)
                         - sqIntImg.at<double>(y1,x0) + sqIntImg.at<double>(y0,x0);

            const int area = (y1 - y0) * (x1 - x0);
            const double rA = 1.0 / area;
            const double mean   = sum * rA;
            const double sqMean = sqSum * rA;
            const double var    = sqMean - mean * mean;

            double dstPix;
            if (var > 1e-6) {
                double srcPix = sRow[x];
                double scale  = std::max(0.0, var - noiseVar) / var;
                dstPix = mean + (srcPix - mean) * scale;
            } else {
                dstPix = mean;
            }
            dRow[x] = (uchar)std::clamp(dstPix, 0.0, 255.0);
        }
    }
}

void wienerDenoiseColor(const Mat& src, Mat& dst,
                        Size windowSize, double coef)
{
    if (src.empty()) { dst = src.clone(); return; }

    if (coef <= 0.0) { dst = src.clone(); return; }

    // Grayscale wiener
    Mat gray = toGray(src);
    if (gray.depth() != CV_8U) gray.convertTo(gray, CV_8U);

    Mat wiened;
    wienerDenoise(gray, wiened, windowSize, 255.0 * coef);

    const int W  = src.cols, H = src.rows;
    const int ch = src.channels();

    dst = src.clone();

    for (int y = 0; y < H; ++y) {
        const uchar* origRow   = src   .ptr<uchar>(y);
        const uchar* grayRow   = gray  .ptr<uchar>(y);
        const uchar* wienRow   = wiened.ptr<uchar>(y);
        uchar*       dstRow    = dst   .ptr<uchar>(y);

        for (int x = 0; x < W; ++x) {
            float origin = grayRow[x];
            float color  = wienRow[x];
            float colScale = (color  + 1.0f) / (origin + 1.0f);
            float colDelta = color - origin * colScale;

            for (int c = 0; c < ch; ++c) {
                int idx = x * ch + c;
                float val = origRow[idx] * colScale + colDelta;
                dstRow[idx] = (uchar)std::clamp(val, 0.0f, 255.0f);
            }
        }
    }
}

} // namespace denoiser
