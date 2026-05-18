// SPDX-License-Identifier: GPL-3.0-or-later
// Inspired by the page-split / gutter detection in scantailor-advanced
// (https://github.com/farfromrefug/scantailor-advanced)
// Reimplemented as a standalone OpenCV-only module — no Qt, no scantailor types.
#pragma once

#include <opencv2/core.hpp>

namespace gutter {

/**
 * @brief Result of gutter detection on a book spread.
 */
struct GutterResult {
    bool foundGutter = false; ///< True if a significant gutter was detected
    bool hasLeft     = false; ///< Left page ROI is valid
    bool hasRight    = false; ///< Right page ROI is valid
    int  gutterX     = -1;   ///< Gutter column in input-image coordinates
    cv::Rect leftPage;        ///< Left page region of interest
    cv::Rect rightPage;       ///< Right page region of interest
};

/**
 * @brief Detect the gutter (binding crease) in a two-page book spread.
 *
 * Uses a scantailor-advanced-inspired algorithm:
 *   1. Portrait images are rejected immediately — book spreads are landscape.
 *   2. Per-column mean brightness:  spine shadow → dark column band.
 *   3. Per-column Sobel horizontal-gradient energy: gutter → low edge content.
 *   4. Combined normalised score (darkness 60 % + low gradient 40 %).
 *   5. Smooth with a Gaussian kernel to suppress isolated dark objects.
 *   6. Find minimum (valley) in the centre 20 – 80 % of the image width.
 *   7. Statistical significance test: valley must be ≥ @p significanceGap
 *      below the mean of the flanking regions (5 – 20 % and 80 – 95 %).
 *      This prevents false detections on single-page documents.
 *
 * @param input            Input image (any depth / channel count; BGR recommended).
 * @param minPageWidthRatio Minimum fraction of image width for each valid page half
 *                          (default 0.20 ⟹ each page must be at least 20 % wide).
 * @param blurSize         (Unused; kept for API compatibility — smoothing is now
 *                          auto-tuned to 3 % of image width.)
 * @param significanceGap  Required score gap between valley and flank mean.
 *                         0.05 = very sensitive, 0.40 = strict. Default 0.15.
 * @return                 GutterResult with gutter position and page ROIs.
 */
GutterResult detectGutter(const cv::Mat& input,
                          float minPageWidthRatio = 0.20f,
                          [[maybe_unused]] int blurSize = 5,
                          float significanceGap   = 0.15f);

} // namespace gutter
