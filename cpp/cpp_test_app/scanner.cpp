#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/utility.hpp>

// #include <tesseract/baseapi.h>

#include <stdio.h>
#include <math.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <DocumentDetector.h>
#include <ColorSimplificationTransform.h>
#include <WhitePaperTransform.h>
#include <WhitePaperTransform2.h>

#include <Utils.h>
#include <stack>
// #include <DocumentOCR.h>
#include <jsoncons/json.hpp>

#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QTimer>

using namespace cv;
using namespace std;

// trim from start (in place)
static inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch)
                                    { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch)
                         { return !std::isspace(ch); })
                .base(),
            s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
    rtrim(s);
    ltrim(s);
}

class DoubleTrack
{
public:
    int int_value = 0;
    double precision;
    double *currentValue;
    void (*user_callback)(double);

    void setup(const std::string &field_name, const std::string &window_name, double *value, double max_value, void (*function)(double), unsigned precision = 100)
    {
        int_value = *value * precision;
        user_callback = function;
        this->precision = precision;
        this->currentValue = value;
        createTrackbar(field_name, window_name, &int_value, max_value * precision, DoubleTrack::callback, this);
    }

    static void callback(int, void *object)
    {
        DoubleTrack *pObject = static_cast<DoubleTrack *>(object);
        *pObject->currentValue = pObject->int_value / pObject->precision;
        pObject->user_callback(*pObject->currentValue);
    }
};

void listFilesInFolder(string dirPath)
{

    for (auto &entry : std::filesystem::directory_iterator(dirPath))
    {
        std::cout << entry.path() << std::endl;
    }
}

bool compareXCords(Point p1, Point p2)
{
    return (p1.x < p2.x);
}

bool compareYCords(Point p1, Point p2)
{
    return (p1.y < p2.y);
}

bool compareDistance(pair<Point, Point> p1, pair<Point, Point> p2)
{
    return (norm(p1.first - p1.second) < norm(p2.first - p2.second));
}

double _distance(Point p1, Point p2)
{
    return sqrt(((p1.x - p2.x) * (p1.x - p2.x)) +
                ((p1.y - p2.y) * (p1.y - p2.y)));
}

void orderPoints(vector<Point> inpts, vector<Point> &ordered)
{
    sort(inpts.begin(), inpts.end(), compareXCords);
    vector<Point> lm(inpts.begin(), inpts.begin() + 2);
    vector<Point> rm(inpts.end() - 2, inpts.end());

    sort(lm.begin(), lm.end(), compareYCords);
    Point tl(lm[0]);
    Point bl(lm[1]);
    vector<pair<Point, Point>> tmp;
    for (size_t i = 0; i < rm.size(); i++)
    {
        tmp.push_back(make_pair(tl, rm[i]));
    }

    sort(tmp.begin(), tmp.end(), compareDistance);
    Point tr(tmp[0].second);
    Point br(tmp[1].second);

    ordered.push_back(tl);
    ordered.push_back(tr);
    ordered.push_back(br);
    ordered.push_back(bl);
}

Mat cropAndWarp(Mat src, vector<cv::Point> orderedPoints)
{
    int newWidth = _distance(orderedPoints[0], orderedPoints[1]);
    int newHeight = _distance(orderedPoints[1], orderedPoints[2]);
    Mat dstBitmapMat = Mat::zeros(newHeight, newWidth, src.type());

    std::vector<Point2f> srcTriangle;
    std::vector<Point2f> dstTriangle;

    srcTriangle.push_back(Point2f(orderedPoints[0].x, orderedPoints[0].y));
    srcTriangle.push_back(Point2f(orderedPoints[1].x, orderedPoints[1].y));
    srcTriangle.push_back(Point2f(orderedPoints[3].x, orderedPoints[3].y));
    srcTriangle.push_back(Point2f(orderedPoints[2].x, orderedPoints[2].y));

    dstTriangle.push_back(Point2f(0, 0));
    dstTriangle.push_back(Point2f(newWidth, 0));
    dstTriangle.push_back(Point2f(0, newHeight));
    dstTriangle.push_back(Point2f(newWidth, newHeight));

    Mat transform = getPerspectiveTransform(srcTriangle, dstTriangle);
    warpPerspective(src, dstBitmapMat, transform, dstBitmapMat.size());
    return dstBitmapMat;
}

detector::DocumentDetector docDetector(300, 0);
int cannyFactor = docDetector.options.cannyFactor * 100;
// int cannyThreshold1 = docDetector.cannyThreshold1;
// int cannyThreshold2 = docDetector.cannyThreshold2;
int morphologyAnchorSize = docDetector.options.morphologyAnchorSize;
int dilateAnchorSize = docDetector.options.dilateAnchorSize;
// int gaussianBlur = docDetector.gaussianBlur;
int medianBlurValue = docDetector.options.medianBlurValue;
int bilateralFilterValue = docDetector.options.bilateralFilterValue;
// int dilateAnchorSizeBefore = docDetector.dilateAnchorSizeBefore;
int houghLinesThreshold = docDetector.options.houghLinesThreshold;
int houghLinesMinLineLength = docDetector.options.houghLinesMinLineLength;
int houghLinesMaxLineGap = docDetector.options.houghLinesMaxLineGap;
int thresh = docDetector.options.thresh;
int threshMax = docDetector.options.threshMax;
// int adapThresholdBlockSize = docDetector.adapThresholdBlockSize; // 391
// int adapThresholdC = docDetector.adapThresholdBlockSize;         // 53
// int gammaCorrection = docDetector.gammaCorrection * 10;          // 53
// int shouldNegate = docDetector.shouldNegate;                     // 53
int useChannel = 0;                                                               // 53
int contoursApproxEpsilonFactor = docDetector.options.contoursApproxEpsilonFactor * 1000; // 53

int whitepaper = 0;
int whitepaper2 = 0;
int enhance = 0;
int enhanceAfter = 0;
int process1 = 0;
int colors = 0;
Mat edged;
Mat warped;
Mat image;
bool canUpdateImage = false;
Mat resizedImage;
int imageIndex = 0;
int colorsResizeThreshold = 100;
int distanceThreshold = 40;
int colorsFilterDistanceThreshold = 20;
int colorSpace = 0;
int paletteColorSpace = 2;
int paletteNbColors = 5;

int dogKSize = 15;
int dogSigma1 = 100.0;
int dogSigma2 = 0.0;

bool tesseractDemo = true;
int actualTesseractDetect = 1;
int desseractDetectContours = 1;

int textDetectDilate = 40; // 0
int textDetect1 = 70;      // 34
int textDetect2 = 4;       // 12

WhitePaperTransformOptions whitepaperOptions;

inline uchar reduceVal(const uchar val)
{
    if (val > 128)
        return 255;
    // if (val > 50) return 128;
    return val;
}

inline uchar reduceVal2(const uchar val)
{
    if (val < 64)
        return 0;
    if (val < 128)
        return 64;
    return 255;
}
void processColors(Mat &img)
{
    Mat dest;
    cvtColor(img, dest, COLOR_BGR2HLS);
    uchar *pixelPtr = dest.data;
    for (int i = 0; i < dest.rows; i++)
    {
        for (int j = 0; j < dest.cols; j++)
        {
            const int pi = i * dest.cols * 3 + j * 3;
            // pixelPtr[pi + 0] = reduceVal(pixelPtr[pi + 0]); // B
            // pixelPtr[pi + 1] = reduceVal2(pixelPtr[pi + 1]); // G
            pixelPtr[pi + 2] = reduceVal2(pixelPtr[pi + 2]); // R
        }
    }
    cvtColor(dest, img, COLOR_HLS2BGR);
}

std::vector<string> images = {};

void setImagesFromFolder(string dirPath)
{
    images.clear();
    for (auto &entry : std::filesystem::directory_iterator(dirPath))
    {
        images.push_back(entry.path());
    }
}

void preprocess_ocr(const Mat &image, const Mat &rgb)
{
    cvtColor(image, rgb, COLOR_BGR2GRAY);
    cv::adaptiveThreshold(rgb, rgb, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 197, 48);
}

// void updateImage()
// {

//     if (!canUpdateImage) {
//         return;
//     }
//     docDetector.options.cannyFactor = cannyFactor / 100;
//     // docDetector.cannyThreshold1 = cannyThreshold1;
//     // docDetector.cannyThreshold2 = cannyThreshold2;
//     docDetector.options.dilateAnchorSize = dilateAnchorSize;
//     // docDetector.dilateAnchorSizeBefore = dilateAnchorSizeBefore;
//     // docDetector.dilateAnchorSizeBefore = dilateAnchorSizeBefore;
//     docDetector.options.houghLinesThreshold = houghLinesThreshold;
//     docDetector.options.houghLinesMinLineLength = houghLinesMinLineLength;
//     docDetector.options.houghLinesMaxLineGap = houghLinesMaxLineGap;
//     // docDetector.adapThresholdBlockSize = adapThresholdBlockSize;
//     // docDetector.adapThresholdC = adapThresholdC;
//     docDetector.options.morphologyAnchorSize = morphologyAnchorSize;
//     // docDetector.shouldNegate = shouldNegate;
//     docDetector.options.useChannel = useChannel - 1;
//     docDetector.options.bilateralFilterValue = bilateralFilterValue;
//     docDetector.options.thresh = thresh;
//     docDetector.options.threshMax = threshMax;
//     // docDetector.gammaCorrection = gammaCorrection / 10.0;
//     docDetector.options.contoursApproxEpsilonFactor = contoursApproxEpsilonFactor / 1000.0;
//     // if (gaussianBlur > 0 && gaussianBlur % 2 == 0)
//     // {
//     //     docDetector.gaussianBlur = gaussianBlur + 1;
//     // }
//     // else
//     // {
//     //     docDetector.gaussianBlur = gaussianBlur;
//     // }
//     if (medianBlurValue > 0 && medianBlurValue % 2 == 0)
//     {
//         docDetector.options.medianBlurValue = medianBlurValue + 1;
//     }
//     else
//     {
//         docDetector.options.medianBlurValue = medianBlurValue;
//     }
//     docDetector.image = image;
//     resizedImage = docDetector.resizeImageMax();

//     detector::DocumentDetector::PageSplitResult split = docDetector.detectGutterAndSplit(resizedImage, 0.4f);

//     vector<vector<cv::Point>> pointsList;
//     // If a gutter was found, scan each page sub-image and merge results into original coordinate system
//     if (split.foundGutter)
//     {
//         Mat combinedEdged = Mat::zeros(resizedImage.size(), CV_8U);
//         // helper lambda to scan a ROI and merge results
//         auto scanAndMerge = [&](const Rect &r) {
//             if (r.width <= 0 || r.height <= 0) return;
//             Mat subImg = resizedImage(r);
//             imshow("subImg", subImg);
//          Mat subEdged;
//             vector<vector<Point>> subList = docDetector.scanPoint(subEdged, subImg, true);
//             // copy subEdged into combinedEdged for display
//             if (!subEdged.empty())
//             {
//                 // ensure types match
//                 if (subEdged.type() != combinedEdged.type()) cv::cvtColor(subEdged, subEdged, COLOR_BGR2GRAY);
//                 subEdged.copyTo(combinedEdged(r));
//             }
//             // offset points from sub-image to full image coordinates (respecting detector scaling)
//             double scaleFactor = docDetector.resizeScale * docDetector.scale;
//             Point offset(static_cast<int>(r.x * scaleFactor), static_cast<int>(r.y * scaleFactor));
//             for (auto &contour : subList)
//             {
//                 for (auto &pt : contour)
//                 {
//                     pt += offset;
//                 }
//                 pointsList.push_back(contour);
//             }
//         };

//         if (split.hasLeft) scanAndMerge(split.leftPage);
//         if (split.hasRight) scanAndMerge(split.rightPage);

//         // if nothing detected on both sides, fallback to whole image scan
//         if (pointsList.empty())
//         {
//             pointsList = docDetector.scanPoint(edged, resizedImage, true);
//         }
//         else
//         {
//             // use combined edged for display
//             edged = combinedEdged;
//         }
//     }
//     else
//     {
//         // no gutter: scan whole image as before
//         pointsList = docDetector.scanPoint(edged, resizedImage, true);
//     }
 
//      if (pointsList.size() == 0)
//      {
//          vector<cv::Point> points;
//          points.push_back(cv::Point(0, 0));
//          points.push_back(cv::Point(image.cols, 0));
//          points.push_back(cv::Point(image.cols, image.rows));
//          points.push_back(cv::Point(0, image.rows));
//          pointsList.push_back(points);
//      }

//     // for (size_t i = 0; i < pointsList.size(); i++)
//     // {
//     //     vector<cv::Point> orderedPoints;
//     //     orderPoints(pointsList[i], orderedPoints);
//     // }

//     if (pointsList.size() > 0)
//     {
//         // cv::polylines(resizedImage, pointsList[0], true, Scalar(255, 0, 0), 2, 8);
//         // vector<cv::Point> orderedPoints;
//         // orderPoints(pointsList[0], orderedPoints);
//         warped = cropAndWarp(image, pointsList[0]);
//         if (whitepaper == 1)
//         {
//             string s;
//             encode_json(whitepaperOptions, s, jsoncons::indenting::no_indent);
//             detector::DocumentDetector::applyTransforms(warped, "whitepaper_" + s);
//         }
//         if (whitepaper2 == 1)
//         {
//             string s;
//             encode_json(whitepaperOptions, s, jsoncons::indenting::no_indent);
//             detector::DocumentDetector::applyTransforms(warped, "whitepaper2_" + s);
//         }
//         if (enhance == 1)
//         {
//             detector::DocumentDetector::applyTransforms(warped, "enhance");
//         }
//         // if (process1 == 1)
//         // {
//         //     // warped = quantizeImage(warped, 2);
//         //     processColors(warped);
//         //     // cv::stylization(warped, warped, 60, 0.07);
//         // }
//         if (colors == 1)
//         {
//             std::stringstream stream;
//             stream << "colors_" << colorsResizeThreshold << "_" << colorsFilterDistanceThreshold << "_" << distanceThreshold << "_" << (colorSpace - 1);
//             // detector::DocumentDetector::applyTransforms(warped, stream.str());
//             std::vector<std::pair<Vec3b, float>> colors = colorSimplificationTransform(warped, warped, false, colorsResizeThreshold, colorsFilterDistanceThreshold, distanceThreshold, paletteNbColors, (ColorSpace)(colorSpace), (ColorSpace)(paletteColorSpace));
//             for (int index = 0; index < colors.size(); ++index)
//             {
//                 auto color = colors.at(index).first;
//                 auto rbgColor = ColorSpaceToBGR(color, (ColorSpace)(colorSpace));
//                 std::stringstream stream;
//                 stream << "\e[48;2;" << (int)rbgColor(2) << ";" << (int)rbgColor(1) << ";" << (int)rbgColor(0) << "m   \e[0m";
//                 // ESC[48;2;⟨r⟩;⟨g⟩;⟨b⟩m
//                 //     __android_log_print(ANDROID_LOG_INFO, "JS", "Color  Color %s Area: %f% %d\n", rgbSexString(HLStoBGR(color.first)).c_str(), 100.f * float(color.second) / n, colors.size());
//                 cout << stream.str() << "Color: " << colors.size() << " - Hue: " << (int)color(0) << " - Lightness: " << (int)color(1) << " - Saturation: " << (int)color(2) << " " << BGRHexString(rbgColor) << " - Area: " << 100.f * (colors.at(index).second) << "%" << endl;
//                 rectangle(warped, cv::Rect(index * 60, 0, 60, 60), Scalar(rbgColor(0), rbgColor(1), rbgColor(2)), -1);
//             }

//             // processColors2(warped);
//             // cv::stylization(warped, warped, 60, 0.07);
//         }
//     }
//     else
//     {
//         warped = Mat();
//     }
//     imshow("SourceImage", resizedImage);
//     imshow("Edges", edged);
//     if (!warped.empty())
//     {

//         // if (tesseractDemo)
//         // {
//         //     // warped = resizeImageToThreshold(warped, 500, 0);
//         //     // Mat toTest;
//         //     // preprocess_ocr(warped, toTest);
//         //     // cvtColor(warped, toTest, COLOR_BGR2GRAY);
//         //     // tesseractTest(warped, warped);
//         //     // detectTextOrientation(toTest);
//         //     // Mat res;
//         //     detector::DocumentOCR::DetectOptions options;
//         //     options.dataPath = "/home/mguillon/Downloads/tesseract/best";
//         //     options.language = "fra";
//         //     options.adapThresholdBlockSize = adapThresholdBlockSize;
//         //     options.adapThresholdC = adapThresholdC;
//         //     options.desseractDetectContours = desseractDetectContours;
//         //     options.tesseractDemo = tesseractDemo;
//         //     options.actualTesseractDetect = actualTesseractDetect;
//         //     options.textDetectDilate = textDetectDilate;
//         //     options.textDetect1 = textDetect1;
//         //     options.textDetect2 = textDetect2;
//         //     double t_r = (double)getTickCount();
//         //     std::optional<detector::DocumentOCR::OCRResult> result = detector::DocumentOCR::detectTextImpl(warped, warped, options, std::nullopt);
//         //     cout << "TIME_OCR = " << ((double)getTickCount() - t_r) * 1000 / getTickFrequency() << endl;
//         //     if (result != std::nullopt)
//         //     {
//         //         float scale_img = 600.f / warped.rows;
//         //         float scale_font = (float)(2 - scale_img) / 1.4f;
//         //         auto ocrResult = *std::move(result);
//         //         for (int j = 0; j < ocrResult.blocks.size(); j++)
//         //         {
//         //             detector::DocumentOCR::OCRData data = ocrResult.blocks[j];
//         //             rectangle(warped, data.box.tl(), data.box.br(), Scalar(255, 0, 255), 3);
//         //             Size word_size = getTextSize(data.text, FONT_HERSHEY_SIMPLEX, (double)scale_font, (int)(3 * scale_font), NULL);
//         //             rectangle(warped, data.box.tl() - Point(3, word_size.height + 3), data.box.tl() + Point(word_size.width, 0), Scalar(255, 0, 255), -1);
//         //             putText(warped, data.text, data.box.tl() - Point(1, 1), FONT_HERSHEY_SIMPLEX, scale_font, Scalar(255, 255, 255), (int)(3 * scale_font));
//         //         }
//         //     }
//         //     // detect_text(warped, warped);
//         // }

//         imshow("Warped", warped);
//     }
//     else
//     {
//         // destroyWindow("Warped");
//         // namedWindow("Warped", WINDOW_KEEPRATIO);
//         // moveWindow("Warped", 900, 100);
//     }
// }


// Enhanced UI State Manager
class UIManager {
public:
    enum class ViewMode {
        SOURCE,
        EDGES,
        WARPED,
        COMPARE
    };
    
    enum class Algorithm {
        NONE,
        WHITEPAPER,
        WHITEPAPER2,
        WHITEPAPER_FAST,
        ENHANCE,
        COLORS
    };
    
    ViewMode currentView = ViewMode::SOURCE;
    Algorithm selectedAlgorithm = Algorithm::NONE;
    
    bool showSourceOverlay = true;
    bool showEdgesOverlay = false;
    bool showWarpedOverlay = false;
    
    std::map<Algorithm, std::string> algorithmNames = {
        {Algorithm::NONE, "None"},
        {Algorithm::WHITEPAPER, "Whitepaper"},
        {Algorithm::WHITEPAPER2, "Whitepaper 2"},
        {Algorithm::WHITEPAPER_FAST, "Whitepaper Fast"},
        {Algorithm::ENHANCE, "Enhance"},
        {Algorithm::COLORS, "Colors"}
    };
    
    std::map<Algorithm, bool> algorithmEnabled = {
        {Algorithm::WHITEPAPER, false},
        {Algorithm::WHITEPAPER2, false},
        {Algorithm::WHITEPAPER_FAST, false},
        {Algorithm::ENHANCE, false},
        {Algorithm::COLORS, false}
    };
    
    void toggleAlgorithm(Algorithm algo) {
        // Disable all others
        for (auto& pair : algorithmEnabled) {
            pair.second = false;
        }
        // Enable selected
        algorithmEnabled[algo] = true;
        selectedAlgorithm = algo;
    }
    
    std::string getStatusText() {
        std::stringstream ss;
        ss << "View: ";
        switch(currentView) {
            case ViewMode::SOURCE: ss << "Source"; break;
            case ViewMode::EDGES: ss << "Edges"; break;
            case ViewMode::WARPED: ss << "Warped"; break;
            case ViewMode::COMPARE: ss << "Compare"; break;
        }
        ss << " | Algorithm: " << algorithmNames[selectedAlgorithm];
        return ss.str();
    }
};



UIManager uiManager;

// Helper function to get window info
struct WindowInfo {
    int width;
    int height;
    float dpiScale;
};


WindowInfo getWindowInfo(const std::string& windowName) {
    WindowInfo info;
    
    // Try to get window from Qt
    QWidget* window = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->windowTitle().toStdString() == windowName) {
            window = widget;
            break;
        }
    }
    
    if (window) {
        // Get actual window size from Qt widget
        info.width = window->width();
        info.height = window->height();
    } else {
        // Fallback to OpenCV method
        auto rect = cv::getWindowImageRect(windowName);
        info.width = rect.width > 0 ? rect.width : 1200;
        info.height = rect.height > 0 ? rect.height : 800;
    }
    
    // Get DPI scale from Qt
    info.dpiScale = 1.0f;
    if (QApplication::primaryScreen()) {
        info.dpiScale = QApplication::primaryScreen()->devicePixelRatio();
    }
    
    return info;
}

void renderUI() {
    // Get actual window dimensions and DPI
    WindowInfo winInfo = getWindowInfo("Document Scanner Test");
    
    // Reserve space for UI elements
    const int statusHeight = 60 * winInfo.dpiScale;
    const int helpHeight = 40 * winInfo.dpiScale;
    const int totalUIHeight = statusHeight + helpHeight;
    
    // Available space for image
    const int availableWidth = winInfo.width;
    const int availableHeight = winInfo.height - totalUIHeight;
    
    // Get the display image based on current view
    Mat display;
    
    switch(uiManager.currentView) {
        case UIManager::ViewMode::SOURCE:
            // Use original image instead of resizedImage for better quality
            display = image.clone();
            break;
        case UIManager::ViewMode::EDGES:
            // Scale edges back to original image size for display
            if (!edged.empty()) {
                Mat edgedDisplay;
                if (edged.channels() == 1) {
                    cvtColor(edged, edgedDisplay, COLOR_GRAY2BGR);
                } else {
                    edgedDisplay = edged.clone();
                }
                // Scale to original image size
                double scaleBack = (double)image.rows / resizedImage.rows;
                resize(edgedDisplay, display, Size(), scaleBack, scaleBack, INTER_LINEAR);
            } else {
                display = image.clone();
            }
            break;
        case UIManager::ViewMode::WARPED:
            if (!warped.empty()) {
                display = warped.clone();
            } else {
                display = Mat::zeros(availableHeight, availableWidth, CV_8UC3);
                putText(display, "No warped image available", 
                       Point(200, 300), FONT_HERSHEY_SIMPLEX, 1, Scalar(255, 255, 255), 2);
            }
            break;
        case UIManager::ViewMode::COMPARE: {
            // Side by side comparison using original image
            Mat left = image.clone();
            Mat right = warped.empty() ? Mat::zeros(image.size(), CV_8UC3) : warped.clone();
            
            // Resize to same height
            if (right.rows != left.rows) {
                double scale = (double)left.rows / right.rows;
                resize(right, right, Size(right.cols * scale, left.rows));
            }
            
            display = Mat(left.rows, left.cols + right.cols + 10, CV_8UC3, Scalar(0, 0, 0));
            left.copyTo(display(Rect(0, 0, left.cols, left.rows)));
            right.copyTo(display(Rect(left.cols + 10, 0, right.cols, right.rows)));
            
            // Draw separator
            line(display, Point(left.cols + 5, 0), Point(left.cols + 5, display.rows), 
                 Scalar(255, 255, 255), 2);
            break;
        }
    }
    
    // Scale image to fit available space while maintaining aspect ratio
    Mat scaledDisplay;
    if (!display.empty()) {
        double scaleX = (double)availableWidth / display.cols;
        double scaleY = (double)availableHeight / display.rows;
        double displayScale = std::min(scaleX, scaleY);
        
        if (displayScale != 1.0) {
            int newWidth = (int)(display.cols * displayScale);
            int newHeight = (int)(display.rows * displayScale);
            resize(display, scaledDisplay, Size(newWidth, newHeight), 0, 0, INTER_LINEAR);
        } else {
            scaledDisplay = display;
        }
    } else {
        scaledDisplay = Mat::zeros(availableHeight, availableWidth, CV_8UC3);
    }
    
    // Center the image in available space
    Mat imageArea = Mat::zeros(availableHeight, availableWidth, CV_8UC3);
    int xOffset = (availableWidth - scaledDisplay.cols) / 2;
    int yOffset = (availableHeight - scaledDisplay.rows) / 2;
    if (xOffset >= 0 && yOffset >= 0) {
        scaledDisplay.copyTo(imageArea(Rect(xOffset, yOffset, scaledDisplay.cols, scaledDisplay.rows)));
    } else {
        scaledDisplay.copyTo(imageArea);
    }
    
    // Create status bar at full window width
    Mat statusBar(statusHeight, availableWidth, CV_8UC3, Scalar(40, 40, 40));
    
    // Scale UI elements based on DPI
    float fontScale = 0.7f * winInfo.dpiScale;
    int thickness = std::max(1, (int)(2 * winInfo.dpiScale));
    
    std::string statusText = uiManager.getStatusText();
    putText(statusBar, statusText, Point(15 * winInfo.dpiScale, 32 * winInfo.dpiScale), 
            FONT_HERSHEY_SIMPLEX, fontScale, Scalar(255, 255, 255), thickness, LINE_AA);
    
    // Add algorithm buttons
    int btnWidth = 80 * winInfo.dpiScale;
    int btnHeight = 40 * winInfo.dpiScale;
    int btnSpacing = 5 * winInfo.dpiScale;
    int btnY = (statusHeight - btnHeight) / 2;
    int totalButtonWidth = 6 * (btnWidth + btnSpacing);
    int btnX = availableWidth - totalButtonWidth - 15 * winInfo.dpiScale;
    
    for (int i = 0; i < 6; i++) {
        UIManager::Algorithm algo = static_cast<UIManager::Algorithm>(i);
        bool isActive = uiManager.algorithmEnabled[algo];
        Scalar btnColor = isActive ? Scalar(0, 200, 0) : Scalar(80, 80, 80);
        Scalar textColor = isActive ? Scalar(255, 255, 255) : Scalar(180, 180, 180);
        
        int x = btnX + i * (btnWidth + btnSpacing);
        rectangle(statusBar, Point(x, btnY), Point(x + btnWidth, btnY + btnHeight), btnColor, -1);
        rectangle(statusBar, Point(x, btnY), Point(x + btnWidth, btnY + btnHeight), 
                 Scalar(200, 200, 200), std::max(1, (int)winInfo.dpiScale), LINE_AA);
        
        std::string shortName = uiManager.algorithmNames[algo];
        if (shortName.length() > 7) shortName = shortName.substr(0, 7);
        
        float btnFontScale = 0.4f * winInfo.dpiScale;
        int baseline = 0;
        Size textSize = getTextSize(shortName, FONT_HERSHEY_SIMPLEX, btnFontScale, 1, &baseline);
        Point textOrg(x + (btnWidth - textSize.width) / 2, btnY + (btnHeight + textSize.height) / 2);
        
        putText(statusBar, shortName, textOrg, 
                FONT_HERSHEY_SIMPLEX, btnFontScale, textColor, 1, LINE_AA);
    }
    
    // Create help bar at full window width
    Mat helpBar(helpHeight, availableWidth, CV_8UC3, Scalar(30, 30, 30));
    std::string helpText = "Keys: [1-4] Views | [Q-Y] Algorithms | [N]ext/[P]rev Image | [Space] Settings | [ESC] Exit";
    float helpFontScale = 0.5f * winInfo.dpiScale;
    putText(helpBar, helpText, Point(15 * winInfo.dpiScale, 23 * winInfo.dpiScale), 
            FONT_HERSHEY_SIMPLEX, helpFontScale, Scalar(200, 200, 200), 1, LINE_AA);
    
    // Combine all elements into final window-sized image
    Mat final(winInfo.height, availableWidth, CV_8UC3, Scalar(0, 0, 0));
    imageArea.copyTo(final(Rect(0, 0, availableWidth, availableHeight)));
    statusBar.copyTo(final(Rect(0, availableHeight, availableWidth, statusHeight)));
    helpBar.copyTo(final(Rect(0, availableHeight + statusHeight, availableWidth, helpHeight)));
    
    imshow("Document Scanner Test", final);
}

void updateImage()
{
    if (!canUpdateImage) {
        return;
    }
    
    // Update detector options
    docDetector.options.cannyFactor = cannyFactor / 100.0;
    docDetector.options.dilateAnchorSize = dilateAnchorSize;
    docDetector.options.houghLinesThreshold = houghLinesThreshold;
    docDetector.options.houghLinesMinLineLength = houghLinesMinLineLength;
    docDetector.options.houghLinesMaxLineGap = houghLinesMaxLineGap;
    docDetector.options.morphologyAnchorSize = morphologyAnchorSize;
    docDetector.options.useChannel = useChannel - 1;
    docDetector.options.bilateralFilterValue = bilateralFilterValue;
    docDetector.options.thresh = thresh;
    docDetector.options.threshMax = threshMax;
    docDetector.options.contoursApproxEpsilonFactor = contoursApproxEpsilonFactor / 1000.0;
    
    if (medianBlurValue > 0 && medianBlurValue % 2 == 0) {
        docDetector.options.medianBlurValue = medianBlurValue + 1;
    } else {
        docDetector.options.medianBlurValue = medianBlurValue;
    }
    
    docDetector.image = image;
    resizedImage = docDetector.resizeImageMax();

    detector::DocumentDetector::PageSplitResult split = docDetector.detectGutterAndSplit(resizedImage, 0.4f);

    vector<vector<cv::Point>> pointsList;
    
    if (split.foundGutter) {
        Mat combinedEdged = Mat::zeros(resizedImage.size(), CV_8U);
        auto scanAndMerge = [&](const Rect &r) {
            if (r.width <= 0 || r.height <= 0) return;
            Mat subImg = resizedImage(r);
            Mat subEdged;
            vector<vector<Point>> subList = docDetector.scanPoint(subEdged, subImg, true);
            
            if (!subEdged.empty()) {
                if (subEdged.type() != combinedEdged.type()) 
                    cv::cvtColor(subEdged, subEdged, COLOR_BGR2GRAY);
                subEdged.copyTo(combinedEdged(r));
            }
            
            double scaleFactor = docDetector.resizeScale * docDetector.scale;
            Point offset(static_cast<int>(r.x * scaleFactor), static_cast<int>(r.y * scaleFactor));
            for (auto &contour : subList) {
                for (auto &pt : contour) {
                    pt += offset;
                }
                pointsList.push_back(contour);
            }
        };

        if (split.hasLeft) scanAndMerge(split.leftPage);
        if (split.hasRight) scanAndMerge(split.rightPage);

        if (pointsList.empty()) {
            pointsList = docDetector.scanPoint(edged, resizedImage, true);
        } else {
            edged = combinedEdged;
        }
    } else {
        pointsList = docDetector.scanPoint(edged, resizedImage, true);
    }
 
    if (pointsList.size() == 0) {
        vector<cv::Point> points;
        points.push_back(cv::Point(0, 0));
        points.push_back(cv::Point(image.cols, 0));
        points.push_back(cv::Point(image.cols, image.rows));
        points.push_back(cv::Point(0, image.rows));
        pointsList.push_back(points);
    }

    if (pointsList.size() > 0) {
        warped = cropAndWarp(image, pointsList[0]);
        
        // Apply selected algorithm
        if (uiManager.algorithmEnabled[UIManager::Algorithm::WHITEPAPER]) {
            string s;
            encode_json(whitepaperOptions, s, jsoncons::indenting::no_indent);
            detector::DocumentDetector::applyTransforms(warped, "whitepaper_" + s);
        }
        else if (uiManager.algorithmEnabled[UIManager::Algorithm::WHITEPAPER2]) {
            string s;
            encode_json(whitepaperOptions, s, jsoncons::indenting::no_indent);
            detector::DocumentDetector::applyTransforms(warped, "whitepaper2_" + s);
        }
        else if (uiManager.algorithmEnabled[UIManager::Algorithm::ENHANCE]) {
            detector::DocumentDetector::applyTransforms(warped, "enhance");
        }
        else if (uiManager.algorithmEnabled[UIManager::Algorithm::COLORS]) {
            std::vector<std::pair<Vec3b, float>> colors = colorSimplificationTransform(
                warped, warped, false, colorsResizeThreshold, colorsFilterDistanceThreshold, 
                distanceThreshold, paletteNbColors, (ColorSpace)(colorSpace), (ColorSpace)(paletteColorSpace));
            
            for (int index = 0; index < colors.size(); ++index) {
                auto color = colors.at(index).first;
                auto rbgColor = ColorSpaceToBGR(color, (ColorSpace)(colorSpace));
                rectangle(warped, cv::Rect(index * 60, 0, 60, 60), 
                         Scalar(rbgColor(0), rbgColor(1), rbgColor(2)), -1);
            }
        }
    } else {
        warped = Mat();
    }
    
    renderUI();
}

void updateSourceImage()
{
    image = imread(images[imageIndex]);
    updateImage();
}

void on_trackbar(int, void *)
{
    updateImage();
}

void on_double_trackbar(double)
{
    updateImage();
}

void on_trackbar_image(int, void *)
{
    updateSourceImage();
}

JSONCONS_N_MEMBER_TRAITS(WhitePaperTransformOptions, 0, csBlackPer, csWhitePer, gaussKSize, gaussSigma, gammaValue, cbBlackPer, cbWhitePer, dogKSize, dogSigma2);

bool settingsVisible = true;

void createSettingsWindow() {
    // destroyWindow("Settings");
    namedWindow("Settings", WINDOW_NORMAL | WINDOW_KEEPRATIO);
    resizeWindow("Settings", 350, 900);
    moveWindow("Settings", 50, 50);
    
    // === NAVIGATION ===
    createTrackbar("Image Index", "Settings", &imageIndex, images.size() - 1, on_trackbar_image);
    
    // === DETECTION SETTINGS ===
    createTrackbar("--- DETECTION ---", "Settings", nullptr, 1, nullptr);
    createTrackbar("Use Channel", "Settings", &useChannel, 3, on_trackbar);
    createTrackbar("Canny Factor", "Settings", &cannyFactor, 400, on_trackbar);
    createTrackbar("Morphology", "Settings", &morphologyAnchorSize, 20, on_trackbar);
    createTrackbar("Dilate", "Settings", &dilateAnchorSize, 20, on_trackbar);
    createTrackbar("Thresh", "Settings", &thresh, 300, on_trackbar);
    createTrackbar("Thresh Max", "Settings", &threshMax, 300, on_trackbar);
    createTrackbar("Contours Eps", "Settings", &contoursApproxEpsilonFactor, 100, on_trackbar);
    
    // === PREPROCESSING ===
    createTrackbar("--- PREPROCESS ---", "Settings", nullptr, 1, nullptr);
    createTrackbar("Bilateral", "Settings", &bilateralFilterValue, 200, on_trackbar);
    createTrackbar("Median Blur", "Settings", &medianBlurValue, 200, on_trackbar);
    
    // === HOUGH LINES ===
    createTrackbar("--- HOUGH LINES ---", "Settings", nullptr, 1, nullptr);
    createTrackbar("Threshold", "Settings", &houghLinesThreshold, 500, on_trackbar);
    createTrackbar("Min Length", "Settings", &houghLinesMinLineLength, 500, on_trackbar);
    createTrackbar("Max Gap", "Settings", &houghLinesMaxLineGap, 500, on_trackbar);
    
    // === WHITEPAPER OPTIONS ===
    createTrackbar("--- WHITEPAPER ---", "Settings", nullptr, 1, nullptr);
    createTrackbar("dogKSize", "Settings", &whitepaperOptions.dogKSize, 100, on_trackbar);
    createTrackbar("dogSigma1", "Settings", &whitepaperOptions.dogSigma1, 200, on_trackbar);
    createTrackbar("dogSigma2", "Settings", &whitepaperOptions.dogSigma2, 100, on_trackbar);
    createTrackbar("csBlackPer", "Settings", &whitepaperOptions.csBlackPer, 100, on_trackbar);
    // createTrackbar("csWhitePer", "Settings", &whitepaperOptions.csWhitePer, 100, on_trackbar);
    createTrackbar("gaussKSize", "Settings", &whitepaperOptions.gaussKSize, 100, on_trackbar);
    // createTrackbar("gaussSigma", "Settings", &whitepaperOptions.gaussSigma, 100, on_trackbar);
    // createTrackbar("gammaValue", "Settings", &whitepaperOptions.gammaValue, 100, on_trackbar);
    
    // === COLORS OPTIONS ===
    createTrackbar("--- COLORS ---", "Settings", nullptr, 1, nullptr);
    createTrackbar("Resize Thresh", "Settings", &colorsResizeThreshold, 500, on_trackbar);
    createTrackbar("Filter Dist", "Settings", &colorsFilterDistanceThreshold, 100, on_trackbar);
    createTrackbar("Distance", "Settings", &distanceThreshold, 100, on_trackbar);
    createTrackbar("Nb Colors", "Settings", &paletteNbColors, 20, on_trackbar);
    createTrackbar("Color Space", "Settings", &colorSpace, 5, on_trackbar);
    createTrackbar("Palette Space", "Settings", &paletteColorSpace, 5, on_trackbar);
}

void handleKeyPress(int key) {
    switch(key) {
        // View modes
        case '1':
            uiManager.currentView = UIManager::ViewMode::SOURCE;
            renderUI();
            break;
        case '2':
            uiManager.currentView = UIManager::ViewMode::EDGES;
            renderUI();
            break;
        case '3':
            uiManager.currentView = UIManager::ViewMode::WARPED;
            renderUI();
            break;
        case '4':
            uiManager.currentView = UIManager::ViewMode::COMPARE;
            renderUI();
            break;
            
        // Algorithms
        case 'q':
        case 'Q':
            uiManager.toggleAlgorithm(UIManager::Algorithm::NONE);
            updateImage();
            break;
        case 'w':
        case 'W':
            uiManager.toggleAlgorithm(UIManager::Algorithm::WHITEPAPER);
            updateImage();
            break;
        case 'e':
        case 'E':
            uiManager.toggleAlgorithm(UIManager::Algorithm::WHITEPAPER2);
            updateImage();
            break;
        case 'r':
        case 'R':
            uiManager.toggleAlgorithm(UIManager::Algorithm::WHITEPAPER_FAST);
            updateImage();
            break;
        case 't':
        case 'T':
            uiManager.toggleAlgorithm(UIManager::Algorithm::ENHANCE);
            updateImage();
            break;
        case 'y':
        case 'Y':
            uiManager.toggleAlgorithm(UIManager::Algorithm::COLORS);
            updateImage();
            break;
            
        // Navigation
        case 'n':
        case 'N':
            imageIndex = (imageIndex + 1) % images.size();
            setTrackbarPos("Image Index", "Settings", imageIndex);
            updateSourceImage();
            break;
        case 'p':
        case 'P':
            imageIndex = (imageIndex - 1 + images.size()) % images.size();
            setTrackbarPos("Image Index", "Settings", imageIndex);
            updateSourceImage();
            break;
            
        // Settings toggle
        case ' ':
            settingsVisible = !settingsVisible;
            if (settingsVisible) {
                createSettingsWindow();
            } else {
                destroyWindow("Settings");
            }
            break;
    }
}

int main(int argc, char **argv)
{
    // Enable high DPI scaling BEFORE creating QApplication
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    
    // Initialize Qt application for proper DPI handling
    QApplication app(argc, argv);

    printf("OpenCV: %s\n", cv::getBuildInformation().c_str());
    
    if (argc < 2) {
        cout << "Usage: ./scanner [test_images_dir_path] [optional: start_image_name]\n";
        return 1;
    }
    
    const char *dirPath = argv[1];
    const char *startImage = argc > 2 ? argv[2] : nullptr;

    setImagesFromFolder(dirPath);
    if (images.empty()) {
        cerr << "No images found in directory: " << dirPath << endl;
        return 1;
    }
    
    if (startImage) {
        auto ret = std::find_if(images.begin(), images.end(), [startImage](string filePath) {
            return filePath.find(startImage) != std::string::npos;
        });
        if (ret != images.end()) {
            imageIndex = ret - images.begin();
        }
    }
    
    // Create main window
    namedWindow("Document Scanner Test", WINDOW_NORMAL | WINDOW_KEEPRATIO | WINDOW_GUI_EXPANDED);
    resizeWindow("Document Scanner Test", 1400, 900);
    
    // Get DPI info
    if (QApplication::primaryScreen()) {
        float dpi = QApplication::primaryScreen()->logicalDotsPerInch();
        float scale = QApplication::primaryScreen()->devicePixelRatio();
        cout << "Display DPI: " << dpi << ", Scale Factor: " << scale << endl;
    }
    
    // Create settings window
    createSettingsWindow();
    
    canUpdateImage = true;
    image = imread(images[imageIndex]);
    updateImage();

    cout << "\n=== Document Scanner Test Interface ===\n";
    cout << "View Modes:\n";
    cout << "  [1] Source Image\n";
    cout << "  [2] Edge Detection\n";
    cout << "  [3] Warped Result\n";
    cout << "  [4] Side-by-Side Compare\n\n";
    cout << "Algorithms:\n";
    cout << "  [Q] None\n";
    cout << "  [W] Whitepaper\n";
    cout << "  [E] Whitepaper 2\n";
    cout << "  [R] Whitepaper Fast\n";
    cout << "  [T] Enhance\n";
    cout << "  [Y] Colors\n\n";
    cout << "Navigation:\n";
    cout << "  [N] Next Image\n";
    cout << "  [P] Previous Image\n";
    cout << "  [Space] Toggle Settings\n";
    cout << "  [ESC] Exit\n\n";

    // Track window for resize detection
    QWidget* mainWindow = nullptr;
    static int lastWidth = 0, lastHeight = 0;
    
    // Timer to check for window resize
    QTimer resizeTimer;
    resizeTimer.setInterval(100);
    QObject::connect(&resizeTimer, &QTimer::timeout, [&]() {
        if (!mainWindow) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->windowTitle() == "Document Scanner Test") {
                    mainWindow = widget;
                    break;
                }
            }
        }
        
        if (mainWindow) {
            int currentWidth = mainWindow->width();
            int currentHeight = mainWindow->height();
            
            if (currentWidth != lastWidth || currentHeight != lastHeight) {
                lastWidth = currentWidth;
                lastHeight = currentHeight;
                if (lastWidth > 0 && lastHeight > 0) {
                    renderUI();
                }
            }
        }
    });
    resizeTimer.start();

    int k;
    while (true) {
        k = waitKey(30);
        if (k == 27) { // ESC
            break;
        } else if (k != -1) {
            handleKeyPress(k);
        }
        
        // Process Qt events to handle window operations
        QApplication::processEvents();
    }

    return 0;
}