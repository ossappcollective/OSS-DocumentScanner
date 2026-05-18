// SPDX-License-Identifier: GPL-3.0-or-later
// Approach inspired by SkewFinder in scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)
// Reimplemented using pure OpenCV — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace skew {

/** Result returned by detectSkew(). */
struct SkewResult {
    double angleDeg;   ///< Detected skew angle in degrees (positive = CCW)
    double confidence; ///< Ratio of best-to-worst projection variance (≥ 1.0)
};

/**
 * @brief Detect document skew via projection-profile analysis.
 *
 * Binarises the input image with Otsu, then for each candidate angle in
 * [−maxAngleDeg, +maxAngleDeg] (step 0.1°) rotates the image and computes the
 * variance of its horizontal projection profile.  Text lines produce a high
 * variance when they are axis-aligned, so the angle with the maximum variance
 * is taken as the skew angle.
 *
 * Approach inspired by the SkewFinder in scantailor-advanced.
 *
 * @param src         Input image (any depth / channel count).
 * @param maxAngleDeg Search range in degrees (symmetric around 0).
 * @return            SkewResult with the best angle and a confidence score.
 */
SkewResult detectSkew(const cv::Mat& src, double maxAngleDeg = 10.0);

/**
 * @brief Rotate an image to correct a detected skew.
 *
 * Uses cv::getRotationMatrix2D + cv::warpAffine with INTER_LINEAR
 * interpolation and a white border fill.
 *
 * @param src       Input image (any depth / channel count).
 * @param angleDeg  Angle to rotate by (degrees, positive = CCW).
 * @return          Corrected image, same size and type as @p src.
 */
cv::Mat correctSkew(const cv::Mat& src, double angleDeg);

} // namespace skew
