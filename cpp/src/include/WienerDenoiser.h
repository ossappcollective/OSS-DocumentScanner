// SPDX-License-Identifier: GPL-3.0-or-later
// Ported and adapted from scantailor-advanced WienerFilter
// (https://github.com/farfromrefug/scantailor-advanced)
// Original by scantailor-advanced contributors.
// Reimplemented using pure OpenCV — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace denoiser {

/** Options bundle for Wiener denoising. */
struct WienerOptions {
    cv::Size windowSize = {5, 5}; ///< Local neighbourhood window
    double   noiseSigma = 10.0;   ///< Assumed noise standard deviation (σ)
    double   colorCoef  = 0.1;    ///< Colour channel scaling coefficient
};

/**
 * @brief Grayscale Wiener filter denoising.
 *
 * For each pixel computes local mean and variance inside a @p windowSize
 * neighbourhood via integral images, then applies the Wiener formula:
 *   dst = mean + (src − mean) × max(0, variance − noiseVariance) / variance
 *
 * Ported from scantailor-advanced WienerFilter.
 *
 * @param src         Input grayscale image (CV_8UC1).
 * @param dst         Output denoised image, same type as @p src.
 * @param windowSize  Local neighbourhood size.
 * @param noiseSigma  Estimated noise standard deviation; noiseVariance = σ².
 */
void wienerDenoise(const cv::Mat& src, cv::Mat& dst,
                   cv::Size windowSize = {5, 5}, double noiseSigma = 10.0);

/**
 * @brief Colour-preserving Wiener filter denoising.
 *
 * Converts to grayscale, applies the Wiener filter, then scales each
 * colour channel proportionally so that hue and saturation are preserved.
 *
 * Ported from scantailor-advanced wienerColorFilter.
 *
 * @param src         Input colour image (CV_8UC3, BGR).
 * @param dst         Output denoised colour image, same type as @p src.
 * @param windowSize  Local neighbourhood size.
 * @param coef        Noise coefficient (fraction of 255): noiseVariance = (255×coef)².
 */
void wienerDenoiseColor(const cv::Mat& src, cv::Mat& dst,
                        cv::Size windowSize = {5, 5}, double coef = 0.1);

} // namespace denoiser
