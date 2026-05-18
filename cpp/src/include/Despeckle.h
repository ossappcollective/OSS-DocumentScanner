// SPDX-License-Identifier: GPL-3.0-or-later
// Ported and adapted from scantailor-advanced Despeckle
// (https://github.com/farfromrefug/scantailor-advanced)
// Original by scantailor-advanced contributors.
// Reimplemented using pure OpenCV — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace speckle {

/** Controls how aggressively small connected components are removed. */
enum class DespeckleLevel {
    CAUTIOUS   = 1, ///< Remove only very tiny speckles  (area <   5 px²)
    NORMAL     = 2, ///< Remove small speckles           (area <  20 px²)
    AGGRESSIVE = 3  ///< Remove larger noise blobs       (area < 100 px²)
};

/**
 * @brief Remove small speckles from a (near-)binary image.
 *
 * Binarises the input with Otsu, finds all connected components, and repaints
 * any component whose area falls below the threshold determined by @p level
 * to white (background).  If the input is colour the removal mask is applied
 * back to the original colour image.
 *
 * Ported from scantailor-advanced Despeckle.
 *
 * @param src    Input image (any depth / channel count).
 * @param dst    Output despeckled image, same type and size as @p src.
 * @param level  Aggressiveness of speckle removal.
 */
void despeckle(const cv::Mat& src, cv::Mat& dst,
               DespeckleLevel level = DespeckleLevel::NORMAL);

/**
 * @brief In-place variant of despeckle().
 *
 * @param img    Image to despeckle in place (any depth / channel count).
 * @param level  Aggressiveness of speckle removal.
 */
void despeckleInPlace(cv::Mat& img, DespeckleLevel level = DespeckleLevel::NORMAL);

} // namespace speckle
