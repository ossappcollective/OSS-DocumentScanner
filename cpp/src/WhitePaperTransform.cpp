#include "./include/Log.h"
#include "./include/WhitePaperTransform.h"
#include <jsoncons/json.hpp>

cv::Mat normalizeKernel(cv::Mat kernel, int kWidth, int kHeight, double scalingFactor = 1.0)
{
    const double K_EPS = 1.0e-12;
    double posRange = 0, negRange = 0;

    for (int i = 0; i < kWidth * kHeight; ++i)
    {
        if (std::abs(kernel.at<double>(i)) < K_EPS)
        {
            kernel.at<double>(i) = 0.0;
        }
        if (kernel.at<double>(i) < 0)
        {
            negRange += kernel.at<double>(i);
        }
        else
        {
            posRange += kernel.at<double>(i);
        }
    }

    double posScale = (std::abs(posRange) >= K_EPS) ? posRange : 1.0;
    double negScale = (std::abs(negRange) >= K_EPS) ? 1.0 : -negRange;

    posScale = scalingFactor / posScale;
    negScale = scalingFactor / negScale;

    for (int i = 0; i < kWidth * kHeight; ++i)
    {
        if (!std::isnan(kernel.at<double>(i)))
        {
            kernel.at<double>(i) *= (kernel.at<double>(i) >= 0) ? posScale : negScale;
        }
    }

    return kernel;
}

cv::Mat dog(const cv::Mat &img, const cv::Mat &dst, int kSize, double sigma1, double sigma2)
{

    // Apply Gaussian blur with the specified kernel radii
//    cv::Mat blurred1, blurred2;
//    GaussianBlur(img, blurred1, cv::Size(kSize, kSize), sigma1);
//    GaussianBlur(img, blurred2, cv::Size(kSize, kSize), sigma2);
//
//    // Compute the Difference of Gaussians (DoG)
//    cv::Mat dogImage = blurred1 - blurred2;
//    return dogImage;
    int kWidth = kSize, kHeight = kSize;
    int x = (kWidth - 1) / 2;
    int y = (kHeight - 1) / 2;
    cv::Mat kernel(kWidth, kHeight, CV_64F, cv::Scalar(0.0));

    // First Gaussian kernel
    if (sigma1 > 0)
    {
        double co1 = 1 / (2 * sigma1 * sigma1);
        double co2 = 1 / (2 * M_PI * sigma1 * sigma1);
        int i = 0;
        for (int v = -y; v <= y; ++v)
        {
            for (int u = -x; u <= x; ++u)
            {
                kernel.at<double>(i) = exp(-(u * u + v * v) * co1) * co2;
                i++;
            }
        }
    }
    // Unity kernel
    else
    {
        kernel.at<double>(x + y * kWidth) = 1.0;
    }

    // Subtract second Gaussian from the kernel
    if (sigma2 > 0)
    {
        double co1 = 1 / (2 * sigma2 * sigma2);
        double co2 = 1 / (2 * M_PI * sigma2 * sigma2);
        int i = 0;
        for (int v = -y; v <= y; ++v)
        {
            for (int u = -x; u <= x; ++u)
            {
                kernel.at<double>(i) -= exp(-(u * u + v * v) * co1) * co2;
                i++;
            }
        }
    }
    // Unity kernel
    else
    {
        kernel.at<double>(x + y * kWidth) -= 1.0;
    }

    // Zero-normalize scaling kernel with a scaling factor of 1.0
    cv::Mat normKernel = normalizeKernel(kernel, kWidth, kHeight, 1.0);

    cv::filter2D(img, dst, -1, normKernel);
    return dst;
}

void negateImage(const cv::Mat &img, const cv::Mat &res)
{
    cv::bitwise_not(img, res);
}

std::vector<int> getBlackWhiteIndices(const cv::Mat &hist, int totCount, int blackCount, int whiteCount)
{
    int blackInd = 0;
    int whiteInd = 255;
    int co = 0;
    for (int i = 0; i < hist.rows; ++i)
    {
        co += hist.at<float>(i);
        if (co > blackCount)
        {
            blackInd = i;
            break;
        }
    }
    co = 0;
    for (int i = hist.rows - 1; i >= 0; --i)
    {
        co += hist.at<float>(i);
        if (co > (totCount - whiteCount))
        {
            whiteInd = i;
            break;
        }
    }
    return {blackInd, whiteInd};
}

void contrastStretch(const cv::Mat &img, cv::Mat &res, int blackPoint, int whitePoint)
{
    int totCount = img.rows * img.cols;
    int blackCount = totCount * blackPoint / 100;
    int whiteCount = totCount * whitePoint / 100;
    std::vector<cv::Mat> chHists;
    int channels = std::min(img.channels(), 3);

    // Calculate histogram for each channel
    for (int i = 0; i < channels; ++i)
    {
        cv::Mat ch;
        cv::extractChannel(img, ch, i);
        cv::Mat hist;
        cv::calcHist(std::vector<cv::Mat>{ch}, {0}, cv::Mat(), hist, {256}, {0, 256});
        chHists.push_back(hist);
    }

    std::vector<std::vector<int>> blackWhiteIndices;
    for (const cv::Mat &hist : chHists)
    {
        blackWhiteIndices.push_back(getBlackWhiteIndices(hist, totCount, blackCount, whiteCount));
    }

    cv::Mat stretchMap(3, 256, CV_8U);

    for (int currCh = 0; currCh < blackWhiteIndices.size(); ++currCh)
    {
        int blackInd = blackWhiteIndices[currCh][0];
        int whiteInd = blackWhiteIndices[currCh][1];
        for (int i = 0; i < stretchMap.cols; ++i)
        {
            if (i < blackInd)
            {
                stretchMap.at<uchar>(currCh, i) = 0;
            }
            else
            {
                if (i > whiteInd)
                {
                    stretchMap.at<uchar>(currCh, i) = 255;
                }
                else
                {
                    if (whiteInd - blackInd > 0)
                    {
                        stretchMap.at<uchar>(currCh, i) = static_cast<uchar>(round((i - blackInd) / static_cast<double>(whiteInd - blackInd) * 255));
                    }
                    else
                    {
                        stretchMap.at<uchar>(currCh, i) = 0;
                    }
                }
            }
        }
    }

    std::vector<cv::Mat> chStretch;
    for (int i = 0; i < channels; ++i)
    {
        cv::Mat ch;
        cv::extractChannel(img, ch, i);
        cv::Mat csCh;
        cv::LUT(ch, stretchMap.row(i), csCh);
        chStretch.push_back(csCh);
    }

    cv::merge(chStretch, res);
}

void fastGaussianBlur(const cv::Mat &img, const cv::Mat &res, int kSize, double sigma)
{
    cv::Mat kernel1D = cv::getGaussianKernel(kSize, sigma);
    cv::sepFilter2D(img, res, -1, kernel1D, kernel1D);
}

void gamma(const cv::Mat &img, const cv::Mat &res, double gammaValue)
{
    double iGamma = 1.0 / gammaValue;
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; ++i)
    {
        lut.at<uchar>(i) = static_cast<uchar>(pow(i / 255.0, iGamma) * 255);
    }
    cv::LUT(img, lut, res);
}
int findLowerBound(const cv::Mat &cumHistSum, int lowCount)
{
    int li = 0;
    int sum = 0;
    for (int i = 0; i < cumHistSum.rows; ++i)
    {
        sum += cumHistSum.at<float>(i);
        if (sum >= lowCount)
        {
            li = i;
            break;
        }
    }
    return li;
}

int findUpperBound(const cv::Mat &cumHistSum, int highCount)
{
    int hi = cumHistSum.rows - 1;
    int sum = 0;
    for (int i = cumHistSum.rows - 1; i >= 0; --i)
    {
        sum += cumHistSum.at<float>(i);
        if (sum >= highCount)
        {
            hi = i;
            break;
        }
    }
    return hi;
}

void colorBalance(const cv::Mat &img, const cv::Mat &res, double lowPer, double highPer)
{
    int totPix = img.cols * img.rows;
    int lowCount = totPix * lowPer / 100;
    int highCount = totPix * (100 - highPer) / 100;

    std::vector<cv::Mat> csImg;

    for (int i = 0; i < img.channels(); ++i)
    {
        cv::Mat ch;
        cv::extractChannel(img, ch, i);
        // calcHist produces a 256×1 column vector — pass it directly to the
        // find helpers.  The previous code called cv::reduce() here which
        // collapsed the 256-row histogram to a single 1×1 scalar, making
        // findLowerBound/findUpperBound always return 0 and rendering the
        // entire colorBalance operation a no-op.
        cv::Mat hist;
        cv::calcHist(std::vector<cv::Mat>{ch}, {0}, cv::Mat(), hist, {256}, {0, 256});

        int li = findLowerBound(hist, lowCount);
        int hi = findUpperBound(hist, highCount);

        if (li == hi)
        {
            csImg.push_back(ch);
            continue;
        }

        cv::Mat lut(1, 256, CV_8U);
        for (int i = 0; i < 256; ++i)
        {
            if (i < li)
            {
                lut.at<uchar>(i) = 0;
            }
            else if (i > hi)
            {
                lut.at<uchar>(i) = 255;
            }
            else if (hi - li > 0)
            {
                lut.at<uchar>(i) = static_cast<uchar>(round((i - li) / static_cast<double>(hi - li) * 255));
            }
            else
            {
                lut.at<uchar>(i) = 0;
            }
        }

        cv::Mat csCh;
        cv::LUT(ch, lut, csCh);
        csImg.push_back(csCh);
    }

    cv::merge(csImg, res);
}

void whiteboardEnhance(const cv::Mat &img, cv::Mat &res, const std::string &optionsJson)
{

    WhitePaperTransformOptions options;

    if (!optionsJson.empty())
    {
        jsoncons::json j = jsoncons::json::parse(optionsJson);

        if (j.contains("dogKSize"))
        {
            options.dogKSize = j["dogKSize"].as<int>();
        }
        if (j.contains("dogSigma1"))
        {
            options.dogSigma1 = j["dogSigma1"].as<int>();
        }
        if (j.contains("dogSigma2"))
        {
            options.dogSigma2 = j["dogSigma2"].as<int>();
        }
        if (j.contains("cbBlackPer"))
        {
            options.cbBlackPer = j["cbBlackPer"].as<int>();
        }
        if (j.contains("cbWhitePer"))
        {
            options.cbWhitePer = j["cbWhitePer"].as<int>();
        }
        if (j.contains("csBlackPer"))
        {
            options.csBlackPer = j["csBlackPer"].as<int>();
        }
        if (j.contains("csWhitePer"))
        {
            options.csWhitePer = j["csWhitePer"].as<int>();
        }
        if (j.contains("gaussKSize"))
        {
            options.gaussKSize = j["gaussKSize"].as<int>();
        }
        if (j.contains("gaussSigma"))
        {
            options.gaussSigma = j["gaussSigma"].as<int>();
        }
        if (j.contains("gammaValue"))
        {
            options.gammaValue = j["gammaValue"].as<int>();
        }
    }
//    auto t_start = std::chrono::high_resolution_clock::now();
    // Difference of Gaussian (DoG)
    res = dog(img, res, options.dogKSize, options.dogSigma1, options.dogSigma2); // 81% time
//    LOGD("WhitePaperTransform dog %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
    // Negative of image
    negateImage(res, res); //0.3% time
//    LOGD("WhitePaperTransform negateImage %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
    // Contrast Stretch (CS)
    contrastStretch(res, res, options.csBlackPer, options.csWhitePer); //10% time
//    LOGD("WhitePaperTransform contrastStretch %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
    // Gaussian Blur
    if (options.gaussKSize > 0) {
        fastGaussianBlur(res, res, options.gaussKSize, options.gaussSigma); //1.6% time
    }
//    LOGD("WhitePaperTransform fastGaussianBlur %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
    // Gamma Correction
    gamma(res, res, options.gammaValue); //0.3% time
//    LOGD("WhitePaperTransform gamma %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
    // Color Balance (CB) (also Contrast Stretch)
    colorBalance(res, res, options.cbBlackPer, options.cbWhitePer); // 5% time
//    LOGD("WhitePaperTransform colorBalance %d ms", (duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count()));
}