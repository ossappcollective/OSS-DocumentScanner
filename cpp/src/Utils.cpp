#include "./include/Utils.h"
// #include <android/log.h>
#include <map>
#include <iomanip>
#include <jsoncons/json.hpp>

using namespace cv;
using namespace std;

struct lessVec3b
{
    bool operator()(const Vec3b &lhs, const Vec3b &rhs) const
    {
        return (lhs[0] != rhs[0]) ? (lhs[0] < rhs[0]) : ((lhs[1] != rhs[1]) ? (lhs[1] < rhs[1]) : (lhs[2] < rhs[2]));
    }
};

// float normDistance(const cv::Vec3b &color1, const cv::Vec3b &color2)
// {
//     // float d1 = abs(color1(0)-color2(0));  // absolute distance
//     // return min(d1,180-d1);
//     return cv::norm(color2 - color1);
//     //     Mat mean;
//     // mean.create(color1.rows, color1.cols, CV_8UC1);
// }
float colorDistance(const cv::Vec3b &color1, const cv::Vec3b &color2, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::HSV || colorSpace == ColorSpace::HSL)
    {
        // Hue goes from 0 to 180, unlike other colour schemes, so it needs
        // to be scaled relative to the other values. It must also wrap
        // around.
        int a = color1(0), b = color2(0);
        int hueDiff;
        if (a > b)
        {
            hueDiff = min(a - b, b - a + 180);
        }
        else
        {
            hueDiff = min(b - a, a - b + 180);
        }

        hueDiff /= 180;

        return sqrt(
            hueDiff * hueDiff +
            (color1(1) - color2(1)) *
                (color1(1) - color2(1)) +
            (color1(2) - color2(2)) *
                (color1(2) - color2(2)));
    }
    // if (color1(2) > saturationLevelMax && color2(2) > saturationLevelMax) {
    //     float d1 = abs(color1(0)-color2(0));  // absolute distance
    // float hDistance = min(d1,180-d1) * 2;
    //     return hDistance;
    // }
    return cv::norm(color2 - color1);

    // float d1 = abs(color1(0) - color2(0)); // absolute distance
    // float hDistance = min(d1, 180 - d1) * 2;
    // if saturation is too low no need to look at value
    // if (color1(2) < saturationLevelMin && color2(2) <saturationLevelMin) {
    //     return abs(color1(1)-color2(1));
    // } else if (abs(color1(1) - color2(1)) > saturationLevelMax) {
    //     return abs(color1(1)-color2(1));
    // } else if (color1(2) < saturationLevelMin && color2(2)  > saturationLevelMax) {
    //     return abs(color1(1)-color2(0));
    // } else if (color2(2) < saturationLevelMin && color1(2)  > saturationLevelMax) {
    //     return abs(color1(0)-color2(1));
    // }
    // return hDistance;
    // return cv::norm(color2 - color1);
    //     Mat mean;
    // mean.create(color1.rows, color1.cols, CV_8UC1);
}

int toBGRColorSpace(ColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case ColorSpace::HSV:
        return cv::COLOR_HSV2BGR;
    case ColorSpace::HSL:
        return cv::COLOR_HLS2BGR;
    case ColorSpace::Lab:
    default:
        return cv::COLOR_Lab2BGR;
    }
}

int toRGBColorSpace(ColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case ColorSpace::HSV:
        return cv::COLOR_HSV2RGB;
    case ColorSpace::HSL:
        return cv::COLOR_HLS2RGB;
    case ColorSpace::Lab:
    default:
        return cv::COLOR_Lab2RGB;
    }
}

int fromBGRColorSpace(ColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case ColorSpace::HSV:
        return cv::COLOR_BGR2HSV;
    case ColorSpace::HSL:
        return cv::COLOR_BGR2HLS;
    case ColorSpace::Lab:
    default:
        return cv::COLOR_BGR2Lab;
    }
}
int fromRGBColorSpace(ColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case ColorSpace::BGR:
        return cv::COLOR_RGB2BGR;
    case ColorSpace::HSV:
        return cv::COLOR_RGB2HSV;
    case ColorSpace::HSL:
        return cv::COLOR_RGB2HLS;
    case ColorSpace::Lab:
    default:
        return cv::COLOR_RGB2Lab;
    }
}

cv::Vec3b ColorSpaceToBGR(cv::Vec3b color, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::BGR)
    {
        return color;
    }
    cv::Mat inMat(1, 1, CV_8UC3);
    inMat.at<cv::Vec3b>(0, 0) = color;
    cv::Mat outMat;

    cv::cvtColor(inMat, outMat, toBGRColorSpace(colorSpace));

    cv::Vec3b result = outMat.at<cv::Vec3b>(0, 0);
    return result;
}

cv::Vec3b ColorSpaceToRGB(cv::Vec3b color, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::BGR)
    {
        return color;
    }
    cv::Mat inMat(1, 1, CV_8UC3);
    inMat.at<cv::Vec3b>(0, 0) = color;
    cv::Mat outMat;

    cv::cvtColor(inMat, outMat, toRGBColorSpace(colorSpace));

    cv::Vec3b result = outMat.at<cv::Vec3b>(0, 0);
    return result;
}
cv::Vec3b RGBToColorSpace(cv::Vec3b color, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::BGR)
    {
        return color;
    }
    cv::Mat inMat(1, 1, CV_8UC3);
    inMat.at<cv::Vec3b>(0, 0) = color;
    cv::Mat outMat;

    cv::cvtColor(inMat, outMat, fromRGBColorSpace(colorSpace));

    cv::Vec3b result = outMat.at<cv::Vec3b>(0, 0);
    return result;
}
cv::Vec3b BGRToColorSpace(cv::Vec3b color, ColorSpace colorSpace)
{
    if (colorSpace == ColorSpace::BGR)
    {
        return color;
    }
    cv::Mat inMat(1, 1, CV_8UC3);
    inMat.at<cv::Vec3b>(0, 0) = color;
    cv::Mat outMat;

    cv::cvtColor(inMat, outMat, fromBGRColorSpace(colorSpace));

    cv::Vec3b result = outMat.at<cv::Vec3b>(0, 0);
    return result;
}

bool compareContourAreas(std::vector<cv::Point> contour1, std::vector<cv::Point> contour2)
{
    double i = fabs(contourArea(cv::Mat(contour1)));
    double j = fabs(contourArea(cv::Mat(contour2)));
    return (i > j);
}

template <typename T>
std::string int_to_hex(T val, size_t width)
{
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(width) << std::hex << (val | 0);
    return ss.str();
}
std::string BGRHexString(cv::Vec3b bgr)
{
    std::stringstream stream;
    stream << "#" << int_to_hex((bgr(2)), 2) << int_to_hex((bgr(1)), 2) << int_to_hex((bgr(0)), 2);
    return stream.str();
}

cv::Mat resizeImageToSize(const cv::Mat &image, int size, int resizeThreshold)
{

    int width = image.cols;
    int height = image.rows;
    if (size > resizeThreshold)
    {
        float resizeScale = 1.0f * size / resizeThreshold;
        width = static_cast<int>(width / resizeScale);
        height = static_cast<int>(height / resizeScale);
        cv::Size size(width, height);
        cv::Mat resizedBitmap(size, CV_8UC3);
        cv::resize(image, resizedBitmap, size);

        return resizedBitmap;
    }
    return image;
}

std::vector<std::pair<Vec3b, float>> getPaletteFrom1Row(const Mat &src, int colorsFilterDistanceThreshold, int nbColors, bool returnAsBGR, ColorSpace colorSpace)
{
    float n = src.rows;
    map<Vec3b, int, lessVec3b> palette;
    for (int r = 0; r < src.rows; ++r)
    {
        Vec3b color = src.at<Vec3b>(r, 0);
        if (palette.count(color) == 0)
        {
            palette[color] = 1;
        }
        else
        {
            palette[color] = palette[color] + 1;
        }
    }
    std::vector<std::pair<Vec3b, float>> pairs;

    for (auto itr = palette.begin(); itr != palette.end(); ++itr)
    {
        if (itr->second / n > 0.001f)
        {
            pairs.push_back(std::pair<Vec3b, float>(BGRToColorSpace(itr->first, colorSpace), itr->second / n));
        }
    }

    sort(pairs.begin(), pairs.end(), [=](std::pair<Vec3b, float> &a, std::pair<Vec3b, float> &b)
         { return a.second > b.second; });
    //    for (auto itr = pairs.begin(); itr != pairs.end(); ++itr)
    //    {
    //        __android_log_print(ANDROID_LOG_INFO, "JS", "Color Hue: %d Saturation: %d Value: %d hex: %s Area: %f%\n", itr->first(0), itr->first(1), itr->first(2), rgbHexString(HLStoBGR(itr->first)).c_str(), 100.f * float(itr->second) / n);
    //    }
    // cout << "Palette colors " << pairs.size() << " "  << nbColors<< endl;
    if (nbColors > 0 && pairs.size() > nbColors)
    {
        // Cap the distance so the loop always terminates (L2 RGB distance max ≈ 441).
        int distanceThreshold = 10;
        while (pairs.size() > nbColors && distanceThreshold <= 512)
        {
            for (int i = pairs.size() - 1; i >= 0; i--)
            {
                Vec3b pixel = (pairs.at(i).first);
                for (int j = i - 1; j >= 0; j--)
                {
                    float distance = colorDistance(pixel, (pairs.at(j).first), colorSpace);
                    if (distance <= distanceThreshold)
                    {
                        pairs.erase(pairs.begin() + i);
                        break;
                    }
                }
                if (pairs.size() <= nbColors)
                {
                    break;
                }
            }
                distanceThreshold += 10;
        }
    }
    else if (colorsFilterDistanceThreshold > 0)
    {
        for (int i = pairs.size() - 1; i >= 0; i--)
        {
            Vec3b pixel = (pairs.at(i).first);
            for (int j = i - 1; j >= 0; j--)
            {
                float distance = colorDistance(pixel, (pairs.at(j).first), colorSpace);
                if (distance <= colorsFilterDistanceThreshold)
                {
                    pairs.erase(pairs.begin() + i);
                    break;
                }
            }
        }
    }

    if (returnAsBGR)
    {
        std::vector<std::pair<Vec3b, float>> pairsBGR;
        for (auto itr = pairs.begin(); itr != pairs.end(); ++itr)
        {
            pairsBGR.push_back(std::pair<Vec3b, float>(ColorSpaceToBGR(itr->first, colorSpace), itr->second));
        }
        return pairsBGR;
    }
    else
    {
        return pairs;
    }
}

std::vector<std::pair<cv::Vec3b, float>> getPalette(const Mat &img, bool isRGB, int resizeThreshold,
                                                int colorsFilterDistanceThreshold, int nbColors, bool returnAsBGR, ColorSpace colorSpace)
{
    int channels = img.channels();
    Mat reducedimage = img;
    if (resizeThreshold > 0) {
        reducedimage = resizeImageToSize(img, max(img.cols, img.rows), resizeThreshold);
    }
    if (channels == 4)
    {
        if (isRGB)
        {
            cvtColor(reducedimage, reducedimage, COLOR_RGBA2BGR);
        }
        else
        {
            //            cvtColor(reducedimage, reducedimage, COLOR_BGRA2BGR);
        }
    }
    else if (isRGB)
    {
        cvtColor(reducedimage, reducedimage, COLOR_RGB2BGR);
    }
    //  cv::cvtColor(img, img, COLOR_BGR2HLS);
    // resize(img,  reducedimage, cv::Size(200, 200));
    int n = reducedimage.rows * reducedimage.cols;
    // Reshape the image to a 2D matrix of pixels
    Mat data = reducedimage.reshape(1, n);
    data.convertTo(data, CV_32F); // Convert to floating point

    // Define criteria, number of clusters (K), and apply kmeans
    TermCriteria criteria(cv::TermCriteria::EPS, 10, 0.1);
    int K = 8;
    Mat labels, centers;
    kmeans(data, K, labels, criteria, 10, KMEANS_PP_CENTERS, centers);

    for (int i = 0; i < n; ++i)
    {
        int cluster_idx = labels.at<int>(i);
        // data.at<Vec3b>(i) = colors.at<Vec3b>(cluster_idx);
        data.at<float>(i, 0) = centers.at<float>(cluster_idx, 0);
        data.at<float>(i, 1) = centers.at<float>(cluster_idx, 1);
        data.at<float>(i, 2) = centers.at<float>(cluster_idx, 2);
    }

    // Mat reduced = data.reshape(3, reducedimage.rows);
    // Mat reduced8;
    data.convertTo(data, CV_8U);

    // Get palette
    return getPaletteFrom1Row(data, colorsFilterDistanceThreshold, nbColors, returnAsBGR, colorSpace);
}
std::string getPaletteString(const Mat &img, bool isRGB, int resizeThreshold,
                                                int colorsFilterDistanceThreshold, int nbColors, bool returnAsBGR, ColorSpace colorSpace) {
    std::vector<std::pair<cv::Vec3b, float>> colors = getPalette(img, isRGB, resizeThreshold, colorsFilterDistanceThreshold, nbColors, returnAsBGR, colorSpace);
    jsoncons::json j(jsoncons::json_array_arg);
    for (int i = 0; i < colors.size(); ++i) {
        j.push_back(BGRHexString(colors[i].first));
    }
    return j.to_string();
}