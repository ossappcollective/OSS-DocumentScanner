// SPDX-License-Identifier: GPL-3.0-or-later
// Algorithms ported and adapted from scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)
// Sauvola / Wolf / Bradley by scantailor-advanced contributors;
// EdgeDiv by zvezdochiot (2023); Grad by zvezdochiot (2024).
// Reimplemented using pure OpenCV — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace adaptive {

/** Options bundle for all adaptive binarization variants. */
struct AdaptiveBinarizeOptions {
    int    windowSize = 25;  ///< Local neighbourhood window side length (odd)
    double k          = 0.34; ///< Primary sensitivity coefficient
    double kep        = 0.5;  ///< EdgeDiv: edge-plus weight
    double kdb        = 0.5;  ///< EdgeDiv: blur-div weight
    double delta      = 0.0;  ///< Threshold offset
};

/**
 * @brief Sauvola local thresholding.
 *
 * For each pixel computes local mean M and standard deviation S over a
 * @p windowSize × @p windowSize neighbourhood using integral images, then:
 *   threshold = M × (1 − k × (1 − S/128 − delta/128))
 *
 * @param src        Input image (any depth; converted to grayscale internally).
 * @param dst        Output CV_8UC1 binary image (0 = black, 255 = white).
 * @param windowSize Side length of the local window (should be odd, ≥ 3).
 * @param k          Sensitivity coefficient (typical: 0.2 – 0.5).
 * @param delta      Constant offset applied to the threshold.
 */
void binarizeSauvola(const cv::Mat& src, cv::Mat& dst,
                     int windowSize = 25, double k = 0.34, double delta = 0.0);

/**
 * @brief Wolf local thresholding.
 *
 * Extends Sauvola by incorporating the global minimum pixel value and the
 * maximum local standard deviation:
 *   threshold = (1−k)×M + k×minGlobal + k×(S/maxS)×(M − minGlobal) + k×(delta/128)
 *
 * @param src        Input image (any depth; converted to grayscale internally).
 * @param dst        Output CV_8UC1 binary image.
 * @param windowSize Side length of the local window (should be odd, ≥ 3).
 * @param k          Sensitivity coefficient (typical: 0.1 – 0.5).
 * @param delta      Constant offset applied to the threshold.
 */
void binarizeWolf(const cv::Mat& src, cv::Mat& dst,
                  int windowSize = 25, double k = 0.3, double delta = 0.0);

/**
 * @brief Bradley integral-image thresholding.
 *
 * Uses the local mean M:  threshold = M × (1 − k) + delta/2
 * Simple and fast; suitable for evenly-lit documents.
 *
 * @param src        Input image (any depth; converted to grayscale internally).
 * @param dst        Output CV_8UC1 binary image.
 * @param windowSize Side length of the local window (should be odd, ≥ 3).
 * @param k          Fraction below local mean to place threshold (typical: 0.1 – 0.2).
 * @param delta      Constant offset applied to the threshold.
 */
void binarizeBradley(const cv::Mat& src, cv::Mat& dst,
                     int windowSize = 25, double k = 0.15, double delta = 0.0);

/**
 * @brief EdgeDiv (EdgePlus & BlurDiv) binarization — zvezdochiot 2023.
 *
 * Blends an edge-enhanced image (EdgePlus) and a blur-divided image (BlurDiv)
 * then applies a global Otsu threshold to the result:
 *   EdgePlus : clamp(pixel + mean − blur, 0, 255)
 *   BlurDiv  : mean>0 ? clamp(pixel×256/(mean+1), 0, 255) : pixel
 *   blend    : (kep×EdgePlus + kdb×BlurDiv) / (kep + kdb)
 *
 * @param src        Input image (any depth; converted to grayscale internally).
 * @param dst        Output CV_8UC1 binary image.
 * @param windowSize Side length of the local window (should be odd, ≥ 3).
 * @param kep        Edge-plus weight.
 * @param kdb        Blur-div weight.
 * @param delta      Offset added to the final Otsu threshold.
 */
void binarizeEdgeDiv(const cv::Mat& src, cv::Mat& dst,
                     int windowSize = 25, double kep = 0.5, double kdb = 0.5,
                     double delta = 0.0);

/**
 * @brief Grad (gradient-snip) binarization — zvezdochiot 2024.
 *
 * Computes local mean M and standard deviation S. A pixel is classified as
 * foreground (black) when: pixel < M − k × (maxStd − S)
 *
 * @param src        Input image (any depth; converted to grayscale internally).
 * @param dst        Output CV_8UC1 binary image.
 * @param windowSize Side length of the local window (should be odd, ≥ 3).
 * @param k          Gradient sensitivity coefficient.
 * @param delta      Constant offset applied to the threshold.
 */
void binarizeGrad(const cv::Mat& src, cv::Mat& dst,
                  int windowSize = 25, double k = 0.3, double delta = 0.0);

} // namespace adaptive
