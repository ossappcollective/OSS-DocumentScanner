#include <DocumentOCR.h>
#include <jsoncons/json.hpp>
#include <tesseract/ocrclass.h>
#include <Utils.h>

using namespace std;
using namespace cv;
using namespace detector;

const char *whitespaceChars = " \t\n\r\f\v";

struct native_data_t {
    tesseract::TessBaseAPI api;
    void *data;
    int lastProgress;
    bool cancel_ocr;

    bool isStateValid() {
        if (!cancel_ocr) {
            return true;
        } else {
            return false;
        }
    }
    void initStateVariables() {
        cancel_ocr = false;
        lastProgress = 0;
    }

    void resetStateVariables() {
        cancel_ocr = false;
        lastProgress = 0;
    }

    native_data_t() {
       lastProgress = 0;
        data = NULL;
        cancel_ocr = false;
    }

    ~native_data_t() {
    }
};

// trim from end of string (right)
inline std::string &rtrim(std::string &s, const char *t = whitespaceChars)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
inline std::string &ltrim(std::string &s, const char *t = whitespaceChars)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (right then left)
inline std::string &trim(std::string &s, const char *t = whitespaceChars)
{
    return ltrim(rtrim(s, t), t);
}

bool isRepetitive(const string &s)
{
    int count = 0;
    for (int i = 0; i < (int)s.size(); i++)
    {
        if ((s[i] == 'i') ||
            (s[i] == 'l') ||
            (s[i] == 'I'))
            count++;
    }
    if (count > ((int)s.size() + 1) / 2)
    {
        return true;
    }
    return false;
}

cv::Mat resizeImageToThreshold(const cv::Mat &image, int resizeThreshold, int borderSize)
{
    // add borders to image
    if (resizeThreshold <= 0)
    {
        return image;
    }
    int width = image.cols;
    int height = image.rows;
    int minSize = max(width, height);
    if (minSize > resizeThreshold)
    {
        float resizeScale = 1.0f * minSize / resizeThreshold;
        width = static_cast<int>(width / resizeScale);
        height = static_cast<int>(height / resizeScale);
        Size size(width, height);
        cv::Mat resizedBitmap(size, image.type());
        resize(image, resizedBitmap, size);
        if (borderSize > 0)
        {
            copyMakeBorder(resizedBitmap, resizedBitmap, borderSize, borderSize, borderSize,
                           borderSize, BORDER_REPLICATE);
        }
        return resizedBitmap;
    }
    if (borderSize > 0)
    {
        Mat resizedBitmap;
        copyMakeBorder(image, resizedBitmap, borderSize, borderSize, borderSize, borderSize,
                       BORDER_REPLICATE);
        return resizedBitmap;
    }
    return image;
}


JSONCONS_ALL_MEMBER_TRAITS(cv::Rect, x, y, width, height);
JSONCONS_N_MEMBER_TRAITS(DocumentOCR::OCRData, 0, fontWeight, text, confidence, fontFamily, fontStyle, textDecoration, fontSize, box);
JSONCONS_ALL_MEMBER_TRAITS(DocumentOCR::OCRResult, text, blocks, imageWidth, imageHeight);


float getYSortValue(const Rect &a)
{
    return (round((a.y + a.height / 2) / 100) * 10000);
}

float getSorkKeyTopToBottomLeftToRight(const Rect &a)
{
    return (getYSortValue(a) + a.x);
}

bool rectComparatorYThenX(const Rect &a, const Rect &b)
{
    return getSorkKeyTopToBottomLeftToRight(a) < getSorkKeyTopToBottomLeftToRight(b);
}

std::optional<DocumentOCR::OCRResult> DocumentOCR::detectTextImpl(const Mat &image, Mat &out_img, const DocumentOCR::DetectOptions &options, std::optional<std::function<void(int)>> const& progressLambda)
{
    //    double t_r = (double) getTickCount();
    std::vector<cv::Rect> boundRects;
    cv::Mat img_gray, img_sobel, img_threshold, element;
    cvtColor(image, img_sobel, image.channels() == 4 ? COLOR_RGBA2GRAY : COLOR_BGR2GRAY);
     if (options.rotation != 0)
    {
        switch (options.rotation)
        {
        case 90:
            rotate(img_sobel, img_sobel, ROTATE_90_CLOCKWISE);
            break;
        case 180:
            rotate(img_sobel, img_sobel, ROTATE_180);
            break;
        default:
            rotate(img_sobel, img_sobel, ROTATE_90_COUNTERCLOCKWISE);
            break;
        }
    }
    cv::adaptiveThreshold(img_sobel, img_sobel, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, options.adapThresholdBlockSize, options.adapThresholdC);

    int imageWidth = img_sobel.size().width;
    int imageHeight = img_sobel.size().height;
    double resizeScale = (double)image.size().width / imageWidth;

    if (options.desseractDetectContours == 1)
    {
        vector<Vec4i> hierarchy;
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Point> contours_poly;
        std::vector<cv::Point> contour;
        element = getStructuringElement(cv::MORPH_RECT, cv::Size(options.textDetect1, options.textDetect2));
        cv::morphologyEx(img_sobel, img_threshold, MORPH_CLOSE, element);

        if (options.textDetectDilate > 0)
        {
            element = getStructuringElement(cv::MORPH_RECT,
                                            cv::Size(options.textDetectDilate, options.textDetectDilate));
            cv::dilate(img_threshold, img_threshold, element, cv::Point(-1, -1),
                       1);
        }
        cv::findContours(img_threshold, contours, hierarchy, cv::RETR_EXTERNAL,
                         cv::CHAIN_APPROX_NONE);
        if (contours.size() > 0)
        {

            std::sort(contours.begin(), contours.end(), compareContourAreas);
            for (int i = 0; i < contours.size(); i++)
            {
                contour = contours[i];
                if (contour.size() > 200)
                {
                    //                    double epsilon = cv::arcLength(contour, true) * contoursApproxEpsilonFactor;
                    cv::approxPolyDP(cv::Mat(contour), contours_poly, 3, true);
                    cv::Rect appRect(boundingRect(cv::Mat(contours_poly)));
                    appRect.x *= resizeScale;
                    appRect.y *= resizeScale;
                    appRect.width *= resizeScale;
                    appRect.height *= resizeScale;
                    // Clamp to img_sobel bounds to prevent an out-of-bounds ROI
                    // when float scaling produces a rect that overshoots the image edge.
                    appRect &= cv::Rect(0, 0, imageWidth, imageHeight);
                    if (appRect.width > 0 && appRect.height > 0 &&
                        appRect.width < 0.9 * imageWidth && appRect.height < 0.9 * imageHeight)
                    {
                        boundRects.push_back(appRect);
                        // if (!out_img.empty())
                        // {
                            // cv::rectangle(out_img, appRect, cv::Scalar(0, 255, 0), 3);
                        // }
                    }
                }
            }
            std::sort(boundRects.begin(), boundRects.end(), rectComparatorYThenX);
        }
        else
        {
            boundRects.push_back(cv::Rect(0, 0, imageWidth, imageHeight));
        }
    }
    else
    {
        boundRects.push_back(cv::Rect(0, 0, imageWidth, imageHeight));
    }

    bitwise_not(img_sobel, img_sobel);
    if (options.actualTesseractDetect == 0)
    {
        return std::nullopt;
    }

    tesseract::TessBaseAPI *api = new tesseract::TessBaseAPI();
    if (api->Init(options.dataPath.c_str(), options.language.c_str(), static_cast<tesseract::OcrEngineMode>(options.oem)) != 0) {
        delete api;
        return std::nullopt;
    }
    api->SetPageSegMode(static_cast<tesseract::PageSegMode>(options.pageSegMode));
    api->SetVariable("user_defined_dpi", options.dpi.c_str());
    string fullText = "";
    float scale_img = 600.f / image.rows;
    float scale_font = (float)(2 - scale_img) / 1.4f;
    vector<OCRData> ocr_data;
    Rect lastBoundRect;
    int boundRectsCount = boundRects.size();
    int boundRectDone = 0;

    std::optional<tesseract::ETEXT_DESC> monitor;
    int16_t lastProgress = 0;
    if (progressLambda != std::nullopt) {
        auto progressCallback = [&] (tesseract::ETEXT_DESC* monitor, int left, int right, int top, int bottom) -> bool {
            int16_t progress = monitor->progress;
            if (progress > lastProgress) {
                progressLambda.value()(boundRectDone + progress/boundRectsCount);
                lastProgress = progress;
            }
            return true;
        };
        monitor = tesseract::ETEXT_DESC();
        monitor->progress_callback2 = progressCallback;
    }
    for (int i = 0; i < boundRectsCount; i++)
    {
        Mat group_img = Mat::zeros(image.rows + 2, image.cols + 2, CV_8UC1);
        img_sobel(boundRects[i]).copyTo(group_img);
        copyMakeBorder(group_img, group_img, 15, 15, 15, 15, BORDER_CONSTANT, Scalar(255));
        api->SetImage(group_img.data, group_img.size().width, group_img.size().height,
                      group_img.channels(), group_img.step1());
        if (monitor != std::nullopt) {
            api->Recognize(&monitor.value());
        } else {
            api->Recognize(0);
        }
        tesseract::ResultIterator *ri = api->GetIterator();
        tesseract::PageIteratorLevel level = static_cast<tesseract::PageIteratorLevel>(options.iteratorLevel);
        if (ri != 0)
        {
            do
            {
                float conf = ri->Confidence(level);
                if (conf == 0)
                {
                    continue;
                }
                const char *word = ri->GetUTF8Text(level);
                if (word == NULL)
                    continue;
                DocumentOCR::OCRData data;
                string stdWord = string(word);
                string trimWord = stdWord;
                trim(trimWord);
                
                int wordSize = stdWord.size();
                if ((wordSize < 2) || (conf < 51) ||
                    ((wordSize == 2) && (stdWord[0] == stdWord[1])) ||
                    ((wordSize < 4) && (conf < 60)) ||
                    isRepetitive(stdWord)) {
                    delete[] word;
                    continue;
                }

                int x1, y1, x2, y2;
                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                if (options.trim) {
                    std::string separator = "";
                    if (!lastBoundRect.empty())
                    {
                        float lastYSortValue = getYSortValue(lastBoundRect);
                        float ySortValue = getYSortValue(boundRects[i]);
                        if (ySortValue == lastYSortValue)
                        {
                            separator = " ";
                        } else {
                            separator = "\n";
                        }
                    }
                    fullText += separator + trimWord;
                } else {
                    fullText += stdWord;
                }
                // we keep "spaces" for full text but remove them as box
                if (trimWord.size() == 0) {
                    delete[] word;
                    continue;
                }
                bool bold;
                bool italic;
                bool underlined;
                bool monospace;
                bool serif;
                bool smallcaps;
                int pointsize;
                int font_id;
                const char *font_name =
                    ri->WordFontAttributes(&bold, &italic, &underlined, &monospace,
                                           &serif, &smallcaps, &pointsize, &font_id);
                if (font_name != 0)
                {
                    data.fontFamily = string(font_name);
                }
                data.text = trimWord;
                data.box = Rect(x1, y1, x2 - x1, y2 - y1);

                data.box.x += boundRects[i].x - 15;
                data.box.y += boundRects[i].y - 15;
                data.confidence = conf;
                data.fontSize = pointsize;
                if (bold)
                {
                    data.fontWeight = "bold";
                }
                if (italic)
                {
                    data.fontStyle = "italic";
                }
                if (underlined)
                {
                    data.textDecoration = "underline";
                }
                if (smallcaps)
                {
                    data.smallcaps = smallcaps;
                }
                ocr_data.push_back(data);

                delete[] word;
            } while (ri->Next(level));
            delete ri;
        }
        if (!fullText.empty())
        {
            lastBoundRect = boundRects[i];
        }
        boundRectDone++;
    }
    api->End();
    delete api;
    if (progressLambda != std::nullopt) {
        progressLambda.value()(100);
    }
    // std::string s;
    if (ocr_data.size() == 0)
    {
        return std::nullopt;
    }
    DocumentOCR::OCRResult result;
    trim(fullText);
    result.text = fullText;
    result.imageWidth = imageWidth;
    result.imageHeight = imageHeight;
    result.blocks = ocr_data;
    //    cout << "TIME_OCR = " << ((double) getTickCount() - t_r) * 1000 / getTickFrequency() << endl;
    return result;
}

std::optional<DocumentOCR::OCRResult> DocumentOCR::detectTextImpl(const Mat &image, const DocumentOCR::DetectOptions &options, std::optional<std::function<void(int)>> const& progressLambda)
{
    Mat output;
    return detectTextImpl(image, output, options, progressLambda);
}

string DocumentOCR::detectText(const Mat &image, const std::string &optionsJson, std::optional<std::function<void(int)>> const& progressLambda)
{
    DetectOptions options;

    if (!optionsJson.empty())
    {
        jsoncons::json j = jsoncons::json::parse(optionsJson);

        if (j.contains("dataPath"))
        {
            options.dataPath = j["dataPath"].as<string>();
        }

        if (j.contains("language"))
        {
            options.language = j["language"].as<string>();
        }
        if (j.contains("dpi"))
        {
            options.dpi = j["dpi"].as<string>();
        }

        if (j.contains("oem"))
        {
            options.oem = j["oem"].as<int>();
        }

        if (j.contains("rotation"))
        {
            options.rotation = j["rotation"].as<int>();
        }

        if (j.contains("pageSegMode"))
        {
            options.pageSegMode = j["pageSegMode"].as<int>();
        }

        if (j.contains("iteratorLevel"))
        {
            options.iteratorLevel = j["iteratorLevel"].as<int>();
        }

        if (j.contains("trim"))
        {
            options.trim = j["trim"].as<bool>();
        }
        if (j.contains("adapThresholdBlockSize"))
        {
            options.adapThresholdBlockSize = j["adapThresholdBlockSize"].as<int>();
        }
        if (j.contains("adapThresholdC"))
        {
            options.adapThresholdC = j["adapThresholdC"].as<int>();
        }
        if (j.contains("detectContours"))
        {
            options.desseractDetectContours = j["detectContours"].as<int>();
        }
        if (j.contains("textDetectDilate"))
        {
            options.textDetectDilate = j["textDetectDilate"].as<int>();
        }
        if (j.contains("textMorphologyEx1"))
        {
            options.textDetect1 = j["textMorphologyEx1"].as<int>();
        }
        if (j.contains("textMorphologyEx2"))
        {
            options.textDetect2 = j["textMorphologyEx2"].as<int>();
        }
    }
    std::optional<OCRResult> result = detectTextImpl(image, options, progressLambda);
    if (result == std::nullopt)
    {
        Mat inversedImage;
        bitwise_not(image, inversedImage);
        result = detectTextImpl(inversedImage, options, progressLambda);
    }
    string s;
    if (result != std::nullopt)
    {
        encode_json(result, s, jsoncons::indenting::no_indent);
    }
    return s;
}
