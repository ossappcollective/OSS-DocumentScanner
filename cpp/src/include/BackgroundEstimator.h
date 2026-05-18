// SPDX-License-Identifier: GPL-3.0-or-later
// Inspired by scantailor-advanced EstimateBackground / PolynomialSurface
// (https://github.com/farfromrefug/scantailor-advanced)
// Original by scantailor-advanced contributors.
// Reimplemented using pure OpenCV — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace bgest {

/** Options for background estimation. */
struct BackgroundEstimatorOptions {
    int   polyDegree     = 4;     ///< Degree of the 2-D fitting polynomial
    float marginFraction = 0.15f; ///< Fraction of image sides used as background sample
};

/**
 * @brief Estimate background illumination as a 2-D polynomial surface.
 *
 * Samples pixels from the outer margin (default: outermost 15 % on each side)
 * which are assumed to be background, fits a 2-D polynomial of degree
 * @p polyDegree to those samples using least-squares, and returns a
 * CV_8UC1 image containing the reconstructed background surface.
 *
 * Inspired by scantailor-advanced EstimateBackground / PolynomialSurface.
 *
 * @param src        Input image (any channel count; converted to grayscale).
 * @param polyDegree Degree of the 2-D polynomial model (1 = plane, 4 = quartic).
 * @return           Background estimate as a CV_8UC1 image.
 */
cv::Mat estimateBackground(const cv::Mat& src, int polyDegree = 4);

/**
 * @brief Normalize illumination by dividing by the estimated background.
 *
 * Computes the polynomial background estimate and divides per pixel:
 *   dst = clamp(src_gray × 255 / max(background, 1), 0, 255)
 * For colour input the same per-pixel scale factor is applied to each
 * channel so that colour balance is preserved.
 *
 * @param src        Input image (any depth / channel count).
 * @param dst        Output normalized image, same type and size as @p src.
 * @param polyDegree Degree of the polynomial model.
 */
void normalizeIllumination(const cv::Mat& src, cv::Mat& dst, int polyDegree = 4);

} // namespace bgest
