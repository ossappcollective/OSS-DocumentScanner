#include <DocumentDetector.h>
#include <WhitePaperTransform.h>
#include <WhitePaperTransform2.h>
#include <ColorSimplificationTransform.h>
#include <Utils.h>
#include <jsoncons/json.hpp>
using namespace detector;
using namespace cv;
using namespace std;

// JSONCONS_ALL_MEMBER_TRAITS(cv::Point, x, y);

DocumentDetector::DocumentDetector(cv::Mat &bitmap, int resizeThreshold, int imageRotation, double scale)
{
    image = bitmap;
    DocumentDetector::resizeThreshold = resizeThreshold;
    DocumentDetector::imageRotation = imageRotation;
    DocumentDetector::scale = scale;
}
DocumentDetector::DocumentDetector(cv::Mat &bitmap, int resizeThreshold, int imageRotation)
{
    image = bitmap;
    DocumentDetector::resizeThreshold = resizeThreshold;
    DocumentDetector::imageRotation = imageRotation;
}

DocumentDetector::DocumentDetector(int resizeThreshold, int imageRotation, double scale)
{
    DocumentDetector::resizeThreshold = resizeThreshold;
    DocumentDetector::imageRotation = imageRotation;
    DocumentDetector::scale = scale;
}
DocumentDetector::DocumentDetector(int resizeThreshold, int imageRotation)
{
    DocumentDetector::resizeThreshold = resizeThreshold;
    DocumentDetector::imageRotation = imageRotation;
}

DocumentDetector::DocumentDetector()
{
}

DocumentDetector::~DocumentDetector()
{
}

void DocumentDetector::updateOptions(std::string _options) {
    if (!_options.empty()) {
        jsoncons::json j = jsoncons::json::parse(_options);

        if (j.contains("areaScaleMinFactor")) {
             options.areaScaleMinFactor = j["areaScaleMinFactor"].as<double>();
        }
        if (j.contains("expectedAreaFactor")) {
             options.expectedAreaFactor = j["expectedAreaFactor"].as<double>();
        }
        if (j.contains("expectedMaxCosine")) {
             options.expectedMaxCosine = j["expectedMaxCosine"].as<double>();
        }
        if (j.contains("expectedOptimalMaxCosine")) {
             options.expectedOptimalMaxCosine = j["expectedOptimalMaxCosine"].as<double>();
        }
        if (j.contains("minDistanceFromBorderFactor")) {
             options.minDistanceFromBorderFactor = j["minDistanceFromBorderFactor"].as<double>();
        }
        if (j.contains("contoursApproxEpsilonFactor")) {
             options.contoursApproxEpsilonFactor = j["contoursApproxEpsilonFactor"].as<double>();
        }
        if (j.contains("borderSize")) {
             options.borderSize = j["borderSize"].as<double>();
        }
    }
}

void sortPoints(std::vector<cv::Point> &points)
{
    // sort by Y
    std::sort(points.begin(), points.end(),
              [](Point const &a, Point const &b)
              { return a.y < b.y; });
    // sort by X
    std::sort(points.begin(), points.begin() + 2,
              [](Point const &a, Point const &b)
              { return a.x < b.x; });
    // sort second half by  X descending
    std::sort(points.begin() + 2, points.end(),
              [](Point const &a, Point const &b)
              { return a.x > b.x; });
}

double angle(cv::Point pt1, cv::Point pt2, cv::Point pt0)
{
    double dx1 = pt1.x - pt0.x;
    double dy1 = pt1.y - pt0.y;
    double dx2 = pt2.x - pt0.x;
    double dy2 = pt2.y - pt0.y;
    return (dx1 * dx2 + dy1 * dy2) /
           sqrt((dx1 * dx1 + dy1 * dy1) * (dx2 * dx2 + dy2 * dy2) + 1e-10);
}

double getContourSortFactor(PointAreaMaxCosMeanCosWeight contour) {
    return /* area */std::get<1>(contour) + /* weight */(double)std::get<4>(contour) * (1 - /* maxCos */std::get<2>(contour));
}
bool sortByArea(PointAreaMaxCosMeanCosWeight contour1, PointAreaMaxCosMeanCosWeight contour2)
{
    return (getContourSortFactor(contour1) > getContourSortFactor(contour2));
}

// Function to compute the intersection point of two line segments defined by Vec4i
bool computeIntersection(const Vec4i &line1, const Vec4i &line2, Point &r)
{
    Point o1(line1[0], line1[1]);
    Point p1(line1[2], line1[3]);
    Point o2(line2[0], line2[1]);
    Point p2(line2[2], line2[3]);

    float angle1 = abs(atan2(o1.y - p1.y, o1.x - p1.x));
    float angle2 = abs(atan2(o2.y - p2.y, o2.x - p2.x));
    if (abs(angle1 - angle2) < 0.4)
    {
        return false;
    }
    Point x = o2 - o1;
    Point d1 = p1 - o1;
    Point d2 = p2 - o2;

    float cross = d1.x * d2.y - d1.y * d2.x;

    if (std::abs(cross) < 1e-5f) {
        return false;
    }
    double t1 = (x.x * d2.y - x.y * d2.x) / cross;
    r = o1 + d1 * t1;
    if (r.x >= 0 && r.y >= 0)
    {
        return true;
    }
    return false;
}

// Function to cluster intersection points based on distance
vector<vector<Point>> clusterIntersectionPoints(const vector<Vec4i> &lines, float maxDistance)
{
    vector<vector<Point>> clusters;
    size_t numLines = lines.size();

    // Compute intersection points for each pair of line segments
    for (size_t i = 0; i < numLines; ++i)
    {
        for (size_t j = i + 1; j < numLines; ++j)
        {
            Point intersection;
            if (computeIntersection(lines[i], lines[j], intersection))
            {
                // Check if the intersection point is close to any existing cluster
                bool clustered = false;
                for (auto &cluster : clusters)
                {
                    for (const auto &point : cluster)
                    {
                        if (norm(point - intersection) < maxDistance)
                        {
                            cluster.push_back(intersection);
                            clustered = true;
                            break;
                        }
                    }
                    if (clustered)
                        break;
                }
                // If not close to any existing cluster, create a new cluster
                if (!clustered)
                {
                    clusters.push_back({intersection});
                }
            }
        }
    }

    return clusters;
}

void DocumentDetector::findSquares(cv::Mat srcGray, double scaledWidth, double scaledHeight,
                                   std::vector<PointAreaMaxCosMeanCosWeight> &squares, cv::Mat drawImage, bool drawContours, float weight)
{
    int marge = static_cast<int>(scaledWidth * options.minDistanceFromBorderFactor) + options.borderSize;
    // Contours search
    std::vector<std::vector<cv::Point>> contours;
    vector<Vec4i> hierarchy;
    cv::findContours(srcGray, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
    
    std::sort(contours.begin(), contours.end(), compareContourAreas);
    // if (drawContours) {
    //     cv::drawContours(drawImage, contours, -1, (0,255,0), 2);
    // }
    std::vector<Point> approx;
    // std::vector<std::vector<Point>> approxs;
    double maxAllowedArea = (scaledWidth - 2 * options.borderSize) * (scaledHeight - 2 * options.borderSize) * 0.92;
    for (size_t i = 0; i < contours.size(); i++)
    {
        std::vector<Point> contour = contours[i];
        double arcLength = cv::arcLength(contour, true);
        double area = cv::contourArea(contour);
        if (arcLength < 100 || area < (scaledWidth * scaledHeight) * options.areaScaleMinFactor || area >= maxAllowedArea)
        {
            continue;
        }

        // Detection of geometric shapes
        double epsilon = arcLength * options.contoursApproxEpsilonFactor;
        cv::approxPolyDP(contour, approx, epsilon, true);
        // approxs.push_back(approx);

        // Detection of quadrilaterals among geometric shapes
        if (approx.size() == 4 && cv::isContourConvex(approx))
        {
            // cv::RotatedRect boundingRect = cv::minAreaRect(contour);
            // Point2f rectPoints[4];
            // boundingRect.points(rectPoints);
            // approx={rectPoints[0], rectPoints[1],rectPoints[2],rectPoints[3]};

            bool shouldIgnore = false;
            for (const cv::Point &p : approx)
            {
                if (p.x < marge || p.x >= scaledWidth - marge || p.y < marge || p.y >= scaledHeight - marge)
                {
                    shouldIgnore = true;
                    break;
                }
            }
            if (shouldIgnore)
            {
                continue;
            }

            // const double area = std::abs(contourArea(approx));
//            if (area > (scaledWidth * scaledHeight) * areaScaleMinFactor)
//            {

                double maxCosine = 0.0;
                double minCosine = 100.0;
                double meanCosine = 0;
                for (int j = 2; j < 6; j++)
                {
                    double cosine = std::abs(angle(approx[j % 4], approx[j - 2], approx[(j - 1) % 4]));
                    maxCosine = std::max(maxCosine, cosine);
                    minCosine = std::min(minCosine, cosine);
                    meanCosine += cosine;
                }
                // Selection of quadrilaterals with large enough angles
                // std::printf("found contour %f %zu %f %f\n", area, approx.size(), minCosine, maxCosine);
                if (maxCosine < options.expectedMaxCosine)
                {
                    // Mat houghLines;
                    if (options.houghLinesThreshold > 0)
                    {
                        Mat houghLines = Mat(srcGray.rows, srcGray.cols, CV_8U, cv::Scalar(0, 0, 0));
                        cv::polylines(houghLines, contour, true, Scalar(255, 0, 0), 5, 8);

                        // std::printf("HoughLinesP %f %f %f\n", houghLinesThreshold, houghLinesMinLineLength, houghLinesMaxLineGap);
                        vector<Vec4i> lineSegments;
                        // will hold the results of the detection
                        cv::HoughLinesP(houghLines, lineSegments, 1, CV_PI / 180, options.houghLinesThreshold, options.houghLinesMinLineLength, options.houghLinesMaxLineGap); // runs the actual detection
                        // Draw the lines

                        if (drawContours)
                        {
                            int distance = 1000;
                            for (const auto &l : lineSegments)
                            {
                                // lines.push_back(convertToPolar(l));
                                double diff = atan2(l[1] - l[3], l[0] - l[2]);
                                int x3 = l[0] + distance * cos(diff);
                                int y3 = l[1] + distance * sin(diff);
                                int x4 = l[0] - distance * cos(diff);
                                int y4 = l[1] - distance * sin(diff);
                                cv::line(drawImage, Point(x3, y3), Point(x4, y4), Scalar(255, 255, 255), 1, cv::LINE_8);
                            }
                        }
                        int maxDistance = min(scaledWidth, scaledHeight) / 10.0;
                        vector<vector<Point>> intersectionClusters = clusterIntersectionPoints(lineSegments, maxDistance); // Adjust distance threshold as needed

                        // // Print the clustered intersection points
                        if (drawContours) {
                            for (size_t i = 0; i < intersectionClusters.size(); ++i)
                            {
                                // cout << "Cluster " << i + 1 << ": ";
                                // for (const auto& point : intersectionClusters[i]) {
                                //     // cout << "(" << point.x << ", " << point.y << ") ";
                                // }
                                circle(drawImage, intersectionClusters[i][0], 4, Scalar(0, 255, 0), FILLED);
                                // cout << endl;
                            }
                        }
                        if (intersectionClusters.size() == 4)
                        {

                            approx[0] = intersectionClusters[0][0];
                            approx[1] = intersectionClusters[1][0];
                            approx[2] = intersectionClusters[2][0];
                            approx[3] = intersectionClusters[3][0];
                            if (drawContours)
                            {
                                sortPoints(approx);
                            }
                        }
                    }
                    // we give more weight for low cosinus (closer to 90d angles)
                    squares.push_back(std::make_tuple(approx, area, maxCosine, meanCosine / 4.0, weight)); //area * weight * (1 - maxCosine))

                    if (drawContours)
                    {
                        cv::polylines(drawImage, approx, true, Scalar(0, 255, 0), 1, 8);
                        cv::polylines(drawImage, contour, true, Scalar(255, 0, 0), 1, 8);
                        // cv::drawContours(drawImage, [approx], -1, Scalar(0, 255, 0), 1);
                    }
                    // if (maxCosine < expectedOptimalMaxCosine && area > (scaledWidth * scaledHeight * expectedAreaFactor))
                    // {
                    //     return true;
                    // }
                }
//            }
        }
    }
    // return false;
}

std::string DocumentDetector::scanPointToJSON()
{
    vector<vector<cv::Point>> result = scanPoint();
    jsoncons::json jsonResult(jsoncons::json_array_arg);
    for (const auto &points : result)
    {
        jsoncons::json jsonResult1(jsoncons::json_array_arg);
        for (const auto &point : points)
        {
            jsonResult1.push_back(jsoncons::json(jsoncons::json_array_arg, {point.x, point.y}));
        }
        jsonResult.push_back(jsonResult1);
    }
    std::string s;
    encode_json(jsonResult, s, jsoncons::indenting::no_indent);
    return s;
}

vector<vector<cv::Point>> DocumentDetector::scanPoint()
{
    Mat edged;
    vector<vector<cv::Point>> result = scanPoint(edged);
    return result;
}
vector<vector<cv::Point>> DocumentDetector::scanPoint(Mat &edged)
{
    resizedImage = resizeImageMax();
    return scanPoint(edged, resizedImage, false);
}
vector<vector<cv::Point>> DocumentDetector::scanPoint(Mat &edged, Mat &image)
{
    return scanPoint(edged, image, false);
}

void correctGamma(const Mat &img, const Mat &dest, const double gamma_)
{
    CV_Assert(gamma_ >= 0);
    //! [changing-contrast-brightness-gamma-correction]
    Mat lookUpTable(1, 256, CV_8U);
    uchar *p = lookUpTable.ptr();
    for (int i = 0; i < 256; ++i)
        p[i] = saturate_cast<uchar>(pow(i / 255.0, gamma_) * 255.0);

    Mat res = img.clone();
    LUT(img, lookUpTable, dest);
    //! [changing-contrast-brightness-gamma-correction]
}

DocumentDetector::PageSplitResult DocumentDetector::detectGutterAndSplit(const Mat& input,
                                      float minPageWidthRatio,
                                      int blurSize)
{
    CV_Assert(!input.empty());

    Mat gray;
    if (input.channels() == 3)
        cvtColor(input, gray, COLOR_BGR2GRAY);
    else
        gray = input.clone();

    // Slight blur reduces noise
    GaussianBlur(gray, gray, Size(blurSize, blurSize), 0);

    // Vertical gradient (detect vertical fold)
    Mat gradX;
    Sobel(gray, gradX, CV_32F, 1, 0, 3);

    gradX = abs(gradX);

    // Sum gradient magnitude per column
    Mat columnEnergy;
    reduce(gradX, columnEnergy, 0, REDUCE_SUM, CV_32F);

    // Convert to vector for easier processing
    vector<float> energy(columnEnergy.cols);
    for (int i = 0; i < columnEnergy.cols; i++)
        energy[i] = columnEnergy.at<float>(0, i);

    // Smooth energy to avoid local noise spikes
    const int smoothRadius = 15;
    vector<float> smoothEnergy(energy.size(), 0);

    for (int i = 0; i < energy.size(); i++) {
        float sum = 0;
        int count = 0;
        for (int j = -smoothRadius; j <= smoothRadius; j++) {
            int idx = i + j;
            if (idx >= 0 && idx < energy.size()) {
                sum += energy[idx];
                count++;
            }
        }
        smoothEnergy[i] = sum / count;
    }

    // Find gutter near center (avoid edges)
    int width = input.cols;
    int searchMin = width * 0.25;
    int searchMax = width * 0.75;

    int gutterX = -1;
    float bestScore = FLT_MAX;

    for (int i = searchMin; i < searchMax; i++) {
        if (smoothEnergy[i] < bestScore) {
            bestScore = smoothEnergy[i];
            gutterX = i;
        }
    }

    DocumentDetector::PageSplitResult result;
    result.gutterX = gutterX;

    if (gutterX < 0)
        return result;

    int minWidth = static_cast<int>(width * minPageWidthRatio);

    // Left page ROI
    if (gutterX > minWidth) {
        result.leftPage = cv::Rect(0, 0, gutterX, input.rows);
        result.hasLeft = true;
    }

    // Right page ROI
    if (width - gutterX > minWidth) {
        result.rightPage = cv::Rect(gutterX, 0, width - gutterX, input.rows);
        result.hasRight = true;
    }

    // mark found gutter if any valid page ROI created
    result.foundGutter = (gutterX >= 0) && (result.hasLeft || result.hasRight);

     return result;
}


vector<vector<cv::Point>> DocumentDetector::scanPoint(Mat &edged, Mat &image, bool drawContours)
{
    // auto t_start = std::chrono::high_resolution_clock::now();
    if (image.empty())
    {
        resizedImage = resizeImageMax();
        image = resizedImage;
    }



    if (imageRotation != 0)
    {
        switch (imageRotation)
        {
        case 90:
            rotate(image, image, ROTATE_90_CLOCKWISE);
            break;
        case 180:
            rotate(image, image, ROTATE_180);
            break;
        default:
            rotate(image, image, ROTATE_90_COUNTERCLOCKWISE);
            break;
        }
    }

    Size size = image.size();
    double width = size.width;
    double height = size.height;
    std::vector<PointAreaMaxCosMeanCosWeight> foundSquares;
    int iterration = 0;
    cv::Mat temp1;
    cv::Mat temp2;
    if (options.medianBlurValue > 0) {
        medianBlur(image, temp1, options.medianBlurValue);
    } else {
        temp1 = image;
    }

    cv::Mat dilateStruct = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(1, (int)options.dilateAnchorSize), std::max(1, (int)options.dilateAnchorSize)));
    cv::Mat morphologyStruct = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(1, (int)options.morphologyAnchorSize), std::max(1, (int)options.morphologyAnchorSize)));
    int channelsCount = std::min(image.channels(), 3);
    int i = channelsCount - 1;
    int minI = 0;
    if (options.useChannel >= 0)
    {
        channelsCount = 1;
        i = options.useChannel;
        minI = options.useChannel;
    }
    // cvtColor(temp1, temp2, COLOR_BGR2GRAY);
    // we give more weight to contours found with threshod then with higher canny
    int weight = 3000000;
    // bool shoudlBreak = false;
    for (i = i; i >= minI; i--)
    {
        //  std::printf("testing on channel %i %i\n", i, iterration);
        cv::extractChannel(temp1, temp2, i);

        cv::threshold(temp2, edged, options.thresh, options.threshMax, cv::THRESH_BINARY);
        cv::morphologyEx(edged, edged, cv::MORPH_CLOSE, morphologyStruct);
        cv::dilate(edged, edged, dilateStruct);
        findSquares(edged, width, height, foundSquares, image, drawContours, (weight--));
        if (foundSquares.size() > 0)
        {
            std::sort(foundSquares.begin(), foundSquares.end(), sortByArea);
            auto firstContour = foundSquares[0];
            if (std::get<2>(firstContour) < options.expectedOptimalMaxCosine && std::get<1>(firstContour) > (width * height * options.expectedAreaFactor))
            {
                // return true;
                i = minI;
                break;
            }
        }
        // if (shoudlBreak)
        // {
        //     break;
        // }
        iterration++;
        // we test over all channels to find the best contour
        int t = 60;
        while (t >= 10)
        {
            cv::Canny(temp2, edged, t * options.cannyFactor, options.cannyFactor * t * 2);
            cv::dilate(edged, edged, dilateStruct);
            findSquares(edged, width, height, foundSquares, image, drawContours, (weight--));
            if (foundSquares.size() > 0)
        {
            std::sort(foundSquares.begin(), foundSquares.end(), sortByArea);
            auto firstContour = foundSquares[0];
            if (std::get<2>(firstContour) < options.expectedOptimalMaxCosine && std::get<1>(firstContour) > (width * height * options.expectedAreaFactor))
            {
                i = minI;
                break;
            }
        }
            iterration++;
            t -= 10;
        }
    }
    if (foundSquares.size() > 0)
    {
      int borderSize = options.borderSize;
      // std::sort(foundSquares.begin(), foundSquares.end(), sortByArea);
        // auto t_end = std::chrono::high_resolution_clock::now();

        // auto elapsed_time_ms = duration_cast<std::chrono::milliseconds>(t_end - t_start);
        // std::printf("found square %i %d ms\n", iterration, elapsed_time_ms.count());
        std::vector<std::vector<Point>> result;
        // for now we return only one. Need to see how to prevent overlapping squares
        for (int i = 0; i < 1; i++)
        {
            std::vector<Point> points = std::get<0>(foundSquares[i]);
            for (int j = 0; j < points.size(); j++)
            {
                if (borderSize > 0)
                {
                    points[j] -= Point(borderSize, borderSize);
                }
                points[j] *= resizeScale * scale;
            }
            sortPoints(points);
            result.push_back(points);
        }

        return result;
    }
    return vector<vector<Point>>();
}
Mat DocumentDetector::resizeImageToSize(int size)
{

    int width = image.cols;
    int height = image.rows;
    int borderSize = options.borderSize;
    if (resizeThreshold > 0 && size > resizeThreshold)
    {
        double widthCoef = width / (double)resizeThreshold;
        double heightCoef = height / (double)resizeThreshold;
        double aspectCoef = std::max(widthCoef, heightCoef);
        resizeScale = aspectCoef;
        width = std::floor(width / aspectCoef);
        height = std::floor(height / aspectCoef);
        Size size(width, height);
        Mat resizedBitmap(size, CV_8UC3);
        resize(image, resizedBitmap, size);
//        imwrite("resized.png", resizedBitmap);
        if (borderSize > 0)
        {
            copyMakeBorder(resizedBitmap, resizedBitmap, borderSize, borderSize, borderSize, borderSize, BORDER_CONSTANT, Scalar(0, 0, 0));
        }
        return resizedBitmap;
    }
    if (borderSize > 0)
    {
        Mat resizedBitmap;
        copyMakeBorder(image, resizedBitmap, borderSize, borderSize, borderSize, borderSize, BORDER_CONSTANT, Scalar(0, 0, 0));
        return resizedBitmap;
    }
    // Return a clone so the caller's Mat does not alias this->image.
    // Without this, an in-place rotate() in scanPoint() would silently mutate
    // the image passed into the DocumentDetector constructor.
    return image.clone();
}
Mat DocumentDetector::resizeImage()
{
    int width = image.cols;
    int height = image.rows;
    int minSize = min(width, height);
    return resizeImageToSize(minSize);
}
Mat DocumentDetector::resizeImageMax()
{
    int width = image.cols;
    int height = image.rows;
    int maxSize = max(width, height);
    return resizeImageToSize(maxSize);
}

std::vector<std::string> split(const std::string& str, const std::string& token) {
    vector<std::string>result;
    std::string toSplit(str);
    while(toSplit.size()){
        int index = toSplit.find(token);
        if(index!=string::npos){
            result.push_back(toSplit.substr(0,index));
            toSplit = toSplit.substr(index+token.size());
            if(toSplit.size()==0)result.push_back(toSplit);
        }else{
            result.push_back(toSplit);
            break;
        }
    }
    return result;
}

void DocumentDetector::applyTransforms(Mat &srcMat, std::string transforms, bool useRGB)
{
    if (srcMat.empty() || transforms.empty()) {
        return;
    }

    try {
        std::vector<std::string> transformArray = split(transforms, "|");
        for (size_t i = 0; i < transformArray.size(); i++)
        {
            std::string transform = transformArray[i];
            std::vector<std::string> options = split(transform, "_");

            // use rfind(,0) instead of starts_with for wider compiler compatibility
            if (transform.rfind("whitepaper2", 0) == 0)
            {
                std::string opt = (options.size() > 1) ? options[1] : std::string();
                try { whiteboardEnhance2(srcMat, srcMat, opt); }
                catch (const std::exception &e) {
                    // __android_log_print(ANDROID_LOG_WARN, TAG, "whitepaper2 failed: %s", e.what());
                }
            }
            else if (transform.rfind("whitepaper", 0) == 0)
            {
                std::string opt = (options.size() > 1) ? options[1] : std::string();
                try { whiteboardEnhance(srcMat, srcMat, opt); }
                catch (const std::exception &e) {
                    // __android_log_print(ANDROID_LOG_WARN, TAG, "whitepaper failed: %s", e.what());
                }
            }
            else if (transform.rfind("enhance", 0) == 0)
            {
                try { cv::detailEnhance(srcMat, srcMat, 10, 0.15); }
                catch (const std::exception &e) {
                    // __android_log_print(ANDROID_LOG_WARN, TAG, "detailEnhance failed: %s", e.what());
                }
            }
            else if (transform.rfind("color", 0) == 0)
            {
                // safer parsing with defaults and catching conversion errors
                int resizeThreshold = 100;
                int colorsFilterDistanceThreshold = 20;
                int distanceThreshold = 40;
                ColorSpace colorSpace = ColorSpace::HSV;
                ColorSpace paletteColorSpace = ColorSpace::BGR;
                int paletteNbColors = 5;
                try {
                    if (options.size() > 1) resizeThreshold = std::stoi(options[1]);
                    if (options.size() > 2) distanceThreshold = std::stoi(options[2]);
                    if (options.size() > 3) paletteNbColors = std::stoi(options[3]);
                    if (options.size() > 4) colorSpace = static_cast<ColorSpace>(std::stoi(options[4]));
                } catch (const std::exception &e) {
                    // __android_log_print(ANDROID_LOG_WARN, TAG, "color transform parse failed: %s", e.what());
                }
                try {
                    colorSimplificationTransform(srcMat, srcMat, useRGB, resizeThreshold, colorsFilterDistanceThreshold, distanceThreshold, paletteNbColors, colorSpace, paletteColorSpace);
                } catch (const std::exception &e) {
                    // __android_log_print(ANDROID_LOG_WARN, TAG, "colorSimplificationTransform failed: %s", e.what());
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        // __android_log_print(ANDROID_LOG_ERROR, TAG, "applyTransforms unexpected error: %s", e.what());
    }
 }
