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

// Build polynomial feature vector for a normalised point in [-1,1]^2.
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

    // ── 1. Collect margin samples (subsampled to ≤ kMaxSamples pts for fast LSQ) ──
    //   Full margin for a 3000×2000 image would be ~3M pixels; we only need
    //   a few thousand well-spread samples for a robust polynomial fit.
    static constexpr int kMaxSamples = 4000;
    std::vector<double> sx, sy, sval;
    {
        // Count total margin pixels first to compute step size
        long totalMargin = 0;
        for (int y = 0; y < H; ++y) {
            bool yM = (y < marginY || y >= H - marginY);
            for (int x = 0; x < W; ++x)
                if (yM || x < marginX || x >= W - marginX)
                    ++totalMargin;
        }
        const int step = std::max(1, (int)(totalMargin / kMaxSamples));
        sx.reserve(kMaxSamples); sy.reserve(kMaxSamples); sval.reserve(kMaxSamples);

        int idx = 0;
        for (int y = 0; y < H; ++y) {
            const uchar* row = gray.ptr<uchar>(y);
            bool yM = (y < marginY || y >= H - marginY);
            for (int x = 0; x < W; ++x) {
                if (yM || x < marginX || x >= W - marginX) {
                    if ((idx % step) == 0) {
                        sx.push_back((x * 2.0 / (W - 1)) - 1.0);
                        sy.push_back((y * 2.0 / (H - 1)) - 1.0);
                        sval.push_back(row[x]);
                    }
                    ++idx;
                }
            }
        }
    }
    if (sval.empty()) return Mat(H, W, CV_8UC1, Scalar(128));

    // ── 2. Build LSQ system and solve ────────────────────────────────────
    const int nPoly = (polyDegree + 1) * (polyDegree + 2) / 2;
    const int N     = (int)sval.size();
    Mat A(N, nPoly, CV_64F);
    Mat b(N, 1,     CV_64F);
    for (int i = 0; i < N; ++i) {
        auto feats = polyFeatures(sx[i], sy[i], polyDegree);
        for (int j = 0; j < nPoly; ++j)
            A.at<double>(i, j) = feats[j];
        b.at<double>(i, 0) = sval[i];
    }
    Mat coeffs;
    solve(A, b, coeffs, DECOMP_SVD);

    // ── 3. Fast vectorised reconstruction ─────────────────────────────────
    //   Pre-compute x-power table: xpow[a][x] = nx^a
    //   Pre-compute y-power table: ypow[b][y] = ny^b
    //   Then bg(y,x) = Σ_k  coeff[k] * xpow[px_k][x] * ypow[py_k][y]
    //   Loop over coefficients (outer) and pixels (inner) → data-cache friendly.

    std::vector<std::vector<double>> xpow(polyDegree + 1, std::vector<double>(W));
    for (int a = 0; a <= polyDegree; ++a)
        for (int x = 0; x < W; ++x) {
            double nx = (x * 2.0 / (W - 1)) - 1.0;
            xpow[a][x] = (a == 0) ? 1.0 : xpow[a-1][x] * nx;
        }

    std::vector<std::vector<double>> ypow(polyDegree + 1, std::vector<double>(H));
    for (int b = 0; b <= polyDegree; ++b)
        for (int y = 0; y < H; ++y) {
            double ny = (y * 2.0 / (H - 1)) - 1.0;
            ypow[b][y] = (b == 0) ? 1.0 : ypow[b-1][y] * ny;
        }

    Mat bgDouble(H, W, CV_64F, Scalar(0.0));
    int coeffIdx = 0;
    for (int d = 0; d <= polyDegree; ++d) {
        for (int j = 0; j <= d; ++j, ++coeffIdx) {
            int px = d - j, py = j;
            double coeff = coeffs.at<double>(coeffIdx, 0);
            const double* xp = xpow[px].data();
            for (int y = 0; y < H; ++y) {
                double yFact      = coeff * ypow[py][y];
                double* bgRow     = bgDouble.ptr<double>(y);
                for (int x = 0; x < W; ++x)
                    bgRow[x] += yFact * xp[x];
            }
        }
    }

    // Clamp and convert to 8-bit
    Mat bg(H, W, CV_8UC1);
    for (int y = 0; y < H; ++y) {
        const double* dr = bgDouble.ptr<double>(y);
        uchar*        br = bg.ptr<uchar>(y);
        for (int x = 0; x < W; ++x)
            br[x] = (uchar)std::clamp(dr[x], 0.0, 255.0);
    }
    return bg;
}

void normalizeIllumination(const Mat& src, Mat& dst, int polyDegree) {
    if (src.empty()) { dst = src.clone(); return; }
    Mat bg = estimateBackground(src, polyDegree);
    if (bg.empty()) { dst = src.clone(); return; }

    const int W = src.cols, H = src.rows;
    const int ch = src.channels();

    // Build per-pixel scale LUT: for bg=0..255 → scale = 255/max(bg,1)
    // Applied per-pixel, per-channel.
    dst = src.clone();
    for (int y = 0; y < H; ++y) {
        const uchar* bgRow = bg .ptr<uchar>(y);
        const uchar* sRow  = src.ptr<uchar>(y);
        uchar*       dRow  = dst.ptr<uchar>(y);

        for (int x = 0; x < W; ++x) {
            double scale = 255.0 / std::max((double)bgRow[x], 1.0);
            for (int c = 0; c < ch; ++c) {
                double val = sRow[x * ch + c] * scale;
                dRow[x * ch + c] = (uchar)std::clamp(val, 0.0, 255.0);
            }
        }
    }
}

} // namespace bgest
