#include "./include/ColorSimplificationTransform.h"
#include "./include/Utils.h"
// #include <android/log.h>

using namespace cv;
using namespace std;

std::vector<std::pair<Vec3b, float>> colorSimplificationTransform(const cv::Mat &img, cv::Mat &res, bool isRGB, int resizeThreshold,
                                                                  int colorsFilterDistanceThreshold, int distanceThreshold, int paletteNbColors, ColorSpace colorSpace, ColorSpace paletteColorSpace)
{
    int channels = img.channels();

    // Get palette
    std::vector<std::pair<Vec3b, float>> colors = getPalette(img, isRGB, resizeThreshold, colorsFilterDistanceThreshold, paletteNbColors, paletteColorSpace != colorSpace, paletteColorSpace);

    if (paletteColorSpace != colorSpace) {
              for (auto itr = colors.begin(); itr != colors.end(); ++itr) {
                itr->first  = BGRToColorSpace(itr->first, colorSpace);
            }

    }

    // OpenCV's BGR2HSV / RGB2HSV converters require exactly 3 input channels.
    // If the source is 4-channel (e.g. RGBA from an Android bitmap), strip the
    // alpha channel first so the colorspace conversion does not throw.
    cv::Mat img3ch;
    if (channels == 4) {
        cvtColor(img, img3ch, isRGB ? COLOR_RGBA2RGB : COLOR_BGRA2BGR);
    } else {
        img3ch = img;
    }

    if (isRGB)
    {
        cvtColor(img3ch, res, fromRGBColorSpace(colorSpace));
    }
    else if (colorSpace != ColorSpace::BGR)
    {
        cv::cvtColor(img3ch, res, fromBGRColorSpace(colorSpace));
    }
    else
    {
        // colorSpace is BGR and input is already BGR/BGRA — use the 3-ch copy
        res = img3ch;
    }

    for (int i = 0; i < res.rows; i++)
    {
        for (int j = 0; j < res.cols; j++)
        {
            Vec3b pixel = (res.at<Vec3b>(i, j));
            for (int k = 0; k < colors.size(); k++)
            {
                Vec3b color = colors.at(k).first;
                if (colorDistance(pixel, color, colorSpace) < distanceThreshold)
                {
                    res.at<Vec3b>(i, j) = color;
                    break;
                }
            }
        }
    }
    if (isRGB)
    {
        cv::cvtColor(res, res, toBGRColorSpace(colorSpace));
    }
    else if (colorSpace != ColorSpace::BGR)
    {
        cv::cvtColor(res, res, toBGRColorSpace(colorSpace));
    }
    return colors;
}

std::vector<std::pair<Vec3b, float>> colorSimplificationTransform(const cv::Mat &img, cv::Mat &res, bool isRGB, int resizeThreshold,
                                                                  int colorsFilterDistanceThreshold, int distanceThreshold, int paletteNbColors, ColorSpace colorSpace)
{
    return colorSimplificationTransform(img, res, isRGB, resizeThreshold, colorsFilterDistanceThreshold, distanceThreshold, paletteNbColors, colorSpace, colorSpace);
}