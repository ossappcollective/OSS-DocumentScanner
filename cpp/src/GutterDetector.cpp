// SPDX-License-Identifier: GPL-3.0-or-later
// Inspired by the page-split / gutter detection in scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)
// Reimplemented as a standalone OpenCV-only module — no Qt, no scantailor types.
//
// Algorithm overview (mirroring scantailor-advanced SplitLinesFinder):
//   1. Downscale to a fast working size (≤ 800 px wide).
//   2. Build a per-column "darkness" profile — a real gutter / spine shows
//      as a dark vertical band due to the binding shadow.
//   3. Build a per-column "content density" profile (horizontal Sobel) —
//      text and line edges add energy everywhere except at the bare gutter.
//   4. Combine both profiles (weighted sum), Gaussian-smooth the result.
//   5. Search for the minimum (valley) in the central 20–80 % of the image.
//   6. Statistical significance gate: the valley must be meaningfully lower
//      than the flanking content regions; otherwise it is a single page.
//   7. Map the detected column back to original-image coordinates.

#include "include/GutterDetector.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace gutter {

GutterResult detectGutter(const cv::Mat& input,
                          float minPageWidthRatio,
                          [[maybe_unused]] int blurSize,
                          float significanceGap)
{
    GutterResult result;
    if (input.empty()) return result;

    // ── 1. Portrait images are never two-page spreads ─────────────────────
    const int origW = input.cols;
    const int origH = input.rows;
    if (origH > origW) return result;

    // ── 2. Downscale for speed (process at ≤ 800 px wide) ─────────────────
    // This dramatically speeds up Sobel and blur on large scans and also
    // acts as a natural low-pass filter removing fine text-edge noise.
    const int maxWorkW = 800;
    cv::Mat gray;
    float downScale = 1.0f;
    {
        cv::Mat tmp;
        if (input.channels() == 1) tmp = input; else cv::cvtColor(input, tmp, cv::COLOR_BGR2GRAY);
        if (tmp.cols > maxWorkW) {
            downScale = (float)maxWorkW / tmp.cols;
            cv::resize(tmp, gray, cv::Size(), downScale, downScale, cv::INTER_AREA);
        } else {
            gray = tmp.clone();
        }
    }

    const int width  = gray.cols;
    const int height = gray.rows;

    // ── 3. Per-column mean brightness profile ─────────────────────────────
    // Binding shadow → dark vertical strip → low colMean near gutter.
    cv::Mat colMeanMat;
    cv::reduce(gray, colMeanMat, 0, cv::REDUCE_AVG, CV_32F);
    std::vector<float> colMean(width);
    for (int i = 0; i < width; ++i)
        colMean[i] = colMeanMat.at<float>(0, i);

    // ── 4. Per-column content density (|dx| energy) ───────────────────────
    // Text/graphics produce large horizontal gradients; the bare gutter
    // has almost no content → low edge energy.
    cv::Mat sobelX;
    cv::Sobel(gray, sobelX, CV_32F, 1, 0, 3);
    cv::Mat absX;
    cv::convertScaleAbs(sobelX, absX);
    cv::Mat colGradMat;
    cv::reduce(absX, colGradMat, 0, cv::REDUCE_AVG, CV_32F);
    std::vector<float> colGrad(width);
    for (int i = 0; i < width; ++i)
        colGrad[i] = colGradMat.at<float>(0, i);

    // ── 5. Normalise both profiles to [0, 1] and combine ──────────────────
    auto normalise = [](std::vector<float>& v) {
        float lo = *std::min_element(v.begin(), v.end());
        float hi = *std::max_element(v.begin(), v.end());
        float range = std::max(hi - lo, 1e-6f);
        for (auto& x : v) x = (x - lo) / range;
    };
    normalise(colMean);
    normalise(colGrad);

    // gutterScore: low = likely gutter
    //   darkness (60%): normalised mean → 0 at darkest column
    //   content  (40%): normalised grad → 0 at emptiest column
    const float kDark = 0.6f, kGrad = 0.4f;
    std::vector<float> score(width);
    for (int i = 0; i < width; ++i)
        score[i] = kDark * colMean[i] + kGrad * colGrad[i];

    // ── 6. Smooth profile to suppress isolated noise spikes ───────────────
    // Kernel width ≈ 3 % of image width (odd, ≥ 5).
    std::vector<float> smooth(width);
    {
        int kw = std::max(5, (int)(width * 0.03f) | 1);  // ensure odd
        cv::Mat scoreRow(1, width, CV_32F, score.data());
        cv::Mat smoothRow;
        cv::GaussianBlur(scoreRow, smoothRow, cv::Size(kw, 1), 0);
        for (int i = 0; i < width; ++i)
            smooth[i] = smoothRow.at<float>(0, i);
    }

    // ── 7. Valley search in central 20–80 % ───────────────────────────────
    const int searchL = (int)(width * 0.20f);
    const int searchR = (int)(width * 0.80f);
    if (searchL >= searchR) return result;

    int   gutterX    = searchL;
    float valleyScore = smooth[searchL];
    for (int i = searchL + 1; i < searchR; ++i) {
        if (smooth[i] < valleyScore) {
            valleyScore = smooth[i];
            gutterX    = i;
        }
    }

    // ── 8. Statistical significance gate ──────────────────────────────────
    // Compare valley to mean of flanking regions (5–20 % and 80–95 %).
    // A real gutter is a notable dark/empty vertical strip; if the "valley"
    // is barely below the flanks, this is a single-page image.
    float flankSum = 0.0f; int flankCnt = 0;
    for (int i = (int)(width * 0.05f); i < searchL; ++i) { flankSum += smooth[i]; ++flankCnt; }
    for (int i = searchR; i < (int)(width * 0.95f); ++i) { flankSum += smooth[i]; ++flankCnt; }
    float flankMean = (flankCnt > 0) ? flankSum / flankCnt : 1.0f;

    if (flankMean - valleyScore < significanceGap)
        return result; // no statistically significant gutter

    // ── 9. Map gutter column back to original image coordinates ───────────
    int origGutterX = (downScale < 1.0f)
                      ? (int)std::round(gutterX / downScale)
                      : gutterX;
    origGutterX = std::clamp(origGutterX, 0, origW - 1);

    result.gutterX = origGutterX;
    const int minWidth = (int)(origW * minPageWidthRatio);

    if (origGutterX > minWidth) {
        result.leftPage = cv::Rect(0, 0, origGutterX, origH);
        result.hasLeft  = true;
    }
    if (origW - origGutterX > minWidth) {
        result.rightPage = cv::Rect(origGutterX, 0, origW - origGutterX, origH);
        result.hasRight  = true;
    }
    result.foundGutter = result.hasLeft || result.hasRight;
    return result;
}

} // namespace gutter
