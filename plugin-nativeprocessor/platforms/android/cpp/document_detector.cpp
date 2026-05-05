#include <jni.h>
#include <string>
#include <android_utils.h>
#include <DocumentDetector.h>
#include <DocumentOCR.h>
#include <Utils.h>
#include <jsoncons/json.hpp>
#include <android/log.h>

#ifdef  WITH_QRCODE
#include <QRCode.h>
#endif

#define TAG "JS"
using namespace std;

//static const char *const kClassDocumentDetector = "com/nativescript/cameraViewDemo/ImageAnalysisCallback";
jstring stringToJavaString(JNIEnv *env, const std::string &stringValue)
{
    return env->NewStringUTF(stringValue.c_str());
}

std::string jstringToString(JNIEnv *env, jstring value)
{
    if (value == nullptr)
    {
        return {};
    }

    jboolean f = JNI_FALSE;
    auto chars = env->GetStringUTFChars(value, &f);
    std::string s(chars);
    env->ReleaseStringUTFChars(value, chars);

    return s;
}

static struct
{
    jclass jClassBitmap;
    jclass jClassBitmapConfig;
    jmethodID jMethodCreateBitmap;
//    jobject jbitmapConfig;

    jclass jClassPoint;
    jclass jClassVector;
    jmethodID jMethodInit;
    jfieldID jFieldIDX;
    jfieldID jFieldIDY;
} gJNIInfo;

static void initClassInfo(JNIEnv *env)
{
    gJNIInfo.jClassBitmap = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass("android/graphics/Bitmap")));
    gJNIInfo.jClassBitmapConfig = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass("android/graphics/Bitmap$Config")));
    gJNIInfo.jMethodCreateBitmap = env->GetStaticMethodID(gJNIInfo.jClassBitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
//    jstring configName = env->NewStringUTF("ARGB_8888");
//    jmethodID valueOfBitmapConfigFunction = env->GetStaticMethodID(jClassBitmapConfig, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;");
//    gJNIInfo.jbitmapConfig = env->CallStaticObjectMethod(jClassBitmapConfig, valueOfBitmapConfigFunction, configName);

    gJNIInfo.jClassPoint = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass("android/graphics/Point")));
    gJNIInfo.jClassVector = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass("java/util/Vector")));
    gJNIInfo.jMethodInit = env->GetMethodID(gJNIInfo.jClassPoint, "<init>", "(II)V");
    gJNIInfo.jFieldIDX = env->GetFieldID(gJNIInfo.jClassPoint, "x", "I");
    gJNIInfo.jFieldIDY = env->GetFieldID(gJNIInfo.jClassPoint, "y", "I");
}

static jobject createJavaPoint(JNIEnv *env, Point point_)
{
    return env->NewObject(gJNIInfo.jClassPoint, gJNIInfo.jMethodInit, point_.x, point_.y);
}

static jobject createBitmap(JNIEnv *env, jint width, jint height)
{
    jstring configName = env->NewStringUTF("ARGB_8888");
    jmethodID valueOfBitmapConfigFunction = env->GetStaticMethodID(gJNIInfo.jClassBitmapConfig, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;");
    jobject jbitmapConfig = env->CallStaticObjectMethod(gJNIInfo.jClassBitmapConfig, valueOfBitmapConfigFunction, configName);
    env->DeleteLocalRef(configName);
    jobject bitmap = env->CallStaticObjectMethod(gJNIInfo.jClassBitmap, gJNIInfo.jMethodCreateBitmap, width, height, jbitmapConfig);
    env->DeleteLocalRef(jbitmapConfig);
    if (!bitmap) {
        return nullptr; // Return null if Bitmap creation failed
    }
    return bitmap;
}

static jstring native_ocr(JNIEnv *env, jobject type, jobject srcBitmap, jstring options_, jobject progressInterface)
{
    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);
    std::string options{jstringToString(env, options_)};
    std::optional<std::function<void(int)>> progressLambda;
    jmethodID method = nullptr;
    if (progressInterface != nullptr)
    {
        jclass objclass = env->GetObjectClass(progressInterface);
        method = env->GetMethodID(objclass, "onProgress", "(I)V");
        progressLambda = [&](int progress)
        {
            env->CallVoidMethod(progressInterface, method, progress);
        };
    }

    std::string result = detector::DocumentOCR::detectText(srcBitmapMat, options, progressLambda);
    return stringToJavaString(env, result);
}


jobject pointsToJava(JNIEnv *env, std::vector<std::vector<cv::Point>> scanPointsList)
{
//     auto t_start = std::chrono::high_resolution_clock::now();
    const int size = (int)scanPointsList.size();
    jmethodID mid = env->GetMethodID(gJNIInfo.jClassVector, "<init>", "()V");
    jmethodID addMethodID = env->GetMethodID(gJNIInfo.jClassVector, "add", "(Ljava/lang/Object;)Z");
    jobject outerVector = env->NewObject(gJNIInfo.jClassVector, mid);
    if (size > 0)
    {
        for (int i = 0; i < size; ++i)
        {
            vector<Point> scanPoints = scanPointsList[i];
            jobject innerVector = env->NewObject(gJNIInfo.jClassVector, mid);
            if (scanPoints.size() == 4)
            {
                for (int j = 0; j < 4; ++j)
                {
                    jobject point = createJavaPoint(env, scanPoints[j]);
                    env->CallBooleanMethod(innerVector, addMethodID, point);
                    env->DeleteLocalRef(point);
                }
            }
            env->CallBooleanMethod(outerVector, addMethodID, innerVector);
            env->DeleteLocalRef(innerVector);
        }
    }

//     auto elapsed_time_ms = duration_cast<std::chrono::milliseconds>(t_end - t_start);
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "pointsToJava %d ms\n", elapsed_time_ms.count());
    return outerVector;
}

//JSONCONS_ALL_MEMBER_TRAITS(cv::Point, x, y);

// static jstring native_scan_qrcode(JNIEnv *env, jobject type, jobject srcBitmap, jint shrunkImageHeight, jint imageRotation, jstring options_, jdouble scale) {
//     Mat srcBitmapMat;
//     bitmap_to_mat(env, srcBitmap, srcBitmapMat);
//     ScanPlusQRCodeResult result;

//     detector::DocumentDetector docDetector(srcBitmapMat, shrunkImageHeight, imageRotation);
//     result.points =  docDetector.scanPoint();
//     std::string options{jstringToString(env, options_)};

//     auto hints = ZXing::DecodeHints().setFormats(ZXing::BarcodeFormat::Any).setMaxNumberOfSymbols(1);
//     result.qrcodes = ZXing::ReadBarcodes(ImageViewFromMat(docDetector.resizeImageToSize(700)), hints);
//     result.resizeScale = docDetector.resizeScale * scale;
//     string s;
//     encode_json(result, s, jsoncons::indenting::no_indent);
//     return stringToJavaString(env, s);
// }
static std::vector<std::vector<cv::Point>> native_scan_mat(Mat &srcBitmapMat, jint shrunkImageHeight, jint imageRotation)
{
    detector::DocumentDetector docDetector(srcBitmapMat, shrunkImageHeight, imageRotation);
    return docDetector.scanPoint();
}
static jobject native_scan(JNIEnv *env, jobject type, jobject srcBitmap, jint shrunkImageHeight, jint imageRotation)
{
    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);
    std::vector<std::vector<cv::Point>> scanPointsList = native_scan_mat(srcBitmapMat,shrunkImageHeight, imageRotation);
    jobject outerVector = pointsToJava(env, scanPointsList);
    srcBitmapMat.release();
    return outerVector;
}

static jobject native_scan_buffer(JNIEnv *env, jobject type, jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                       jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                       jint rowStride3, jint shrunkImageHeight, jint imageRotation)
{
    Mat srcBitmapMat;
    buffer_to_mat(env, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, srcBitmapMat);
    std::vector<std::vector<cv::Point>> scanPointsList = native_scan_mat(srcBitmapMat,shrunkImageHeight, imageRotation);
    jobject outerVector = pointsToJava(env, scanPointsList);
    srcBitmapMat.release();
    return outerVector;
}

static void testImageProxyToBitmap(JNIEnv *env, jobject type, jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                      jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                      jint rowStride3, jobject out_bitmap) {
    Mat srcBitmapMat;
    buffer_to_mat(env, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, srcBitmapMat);
    mat_to_bitmap(env, srcBitmapMat, out_bitmap);
    srcBitmapMat.release();
}
static std::string native_scan_json_mat(JNIEnv *env, Mat &srcBitmapMat, jint shrunkImageHeight, jint imageRotation, jdouble scale, jstring _options) {
    detector::DocumentDetector docDetector(srcBitmapMat, shrunkImageHeight, imageRotation, scale);
    if (shrunkImageHeight == 0) {
        // image was resized on the native side let diminish blur
        docDetector.options.medianBlurValue = 3;
    }
    std::string options{jstringToString(env, _options)};
    docDetector.updateOptions(options);
    return docDetector.scanPointToJSON();
}
static jstring native_scan_json(JNIEnv *env, jobject type, jobject srcBitmap, jint shrunkImageHeight, jint imageRotation, jdouble scale, jstring options)
{
//    auto t_start = std::chrono::high_resolution_clock::now();
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "OpenCV: %s", cv::getBuildInformation().c_str());

    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "bitmap_to_mat %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
    std::string scanPointsList = native_scan_json_mat(env, srcBitmapMat, shrunkImageHeight, imageRotation, scale, options);
    srcBitmapMat.release();
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "native_scan_json_mat %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
    return stringToJavaString(env, scanPointsList);
}
static jstring native_color_palette(JNIEnv *env, jobject type, jobject srcBitmap, jint shrunkImageHeight, jint colorsFilterDistanceThreshold, jint nbColors, jint colorPalette)
{
    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);
    // __android_log_print(ANDROID_LOG_INFO, TAG, "native_color_palette %d %d %d %d\n", shrunkImageHeight, colorsFilterDistanceThreshold, nbColors, colorPalette);
   std::string colors = getPaletteString(srcBitmapMat, true, shrunkImageHeight, colorsFilterDistanceThreshold, nbColors, true, (ColorSpace)colorPalette);
    srcBitmapMat.release();
    return stringToJavaString(env, colors);
}
static jstring native_scan_json_buffer(JNIEnv *env, jobject type, jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                       jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                       jint rowStride3, jint shrunkImageHeight, jint imageRotation)
{
//     auto t_start = std::chrono::high_resolution_clock::now();
    Mat srcBitmapMat;
    buffer_to_mat(env, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, srcBitmapMat);
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "buffer_to_mat %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
    std::string scanPointsList = native_scan_json_mat(env, srcBitmapMat, shrunkImageHeight, imageRotation, 1.0, nullptr);
    srcBitmapMat.release();
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "native_scan_json_mat %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
    return stringToJavaString(env, scanPointsList);
}
static jstring native_scan_json_buffer_with_image(JNIEnv *env, jobject type, jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                       jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                       jint rowStride3, jint shrunkImageHeight, jint imageRotation, jobject outBitmap)
{
    Mat srcBitmapMat;
    buffer_to_mat(env, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, srcBitmapMat);
    std::string scanPointsList = native_scan_json_mat(env, srcBitmapMat, shrunkImageHeight, imageRotation, 1.0, nullptr);
    mat_to_bitmap(env, srcBitmapMat, outBitmap);
    srcBitmapMat.release();
    return stringToJavaString(env, scanPointsList);
}
//static jstring native_scan_json_buffer_with_image_one_plane(JNIEnv *env, jobject type, jint width, jint height, jobject buffer1,
//                                       jint rowStride1, jint shrunkImageHeight, jint imageRotation, jobject outBitmap)
//{
//    const char *pixels = static_cast<const char *>(env->GetDirectBufferAddress(buffer1));
//    vector<uchar> jpgbytes(pixels, pixels+rowStride1);
//    Mat srcBitmapMat = imdecode(jpgbytes, 0);
////    Mat srcBitmapMat(cv::Size(width, height), CV_8UC1, pixels, rowStride1);
//    std::string scanPointsList = native_scan_json_mat(srcBitmapMat, shrunkImageHeight, imageRotation);
//    mat_to_bitmap(env, srcBitmapMat, outBitmap);
//    srcBitmapMat.release();
//    return stringToJavaString(env, scanPointsList);
//}

//static vector<Point> pointsToNative(JNIEnv *env, jobjectArray points_)
//{
//    int arrayLength = env->GetArrayLength(points_);
//    vector<Point> result;
//    for (int i = 0; i < arrayLength; i++)
//    {
//        jobject point_ = env->GetObjectArrayElement(points_, i);
//        int pX = env->GetIntField(point_, gPointInfo.jFieldIDX);
//        int pY = env->GetIntField(point_, gPointInfo.jFieldIDY);
//        result.push_back(Point(pX, pY));
//    }
//    return result;
//}

static void native_crop(JNIEnv *env, jobject type, jobject srcBitmap, jstring points_, jstring transforms, jobject outBitmap)
{
//    auto t_start = std::chrono::high_resolution_clock::now();
    std::string points{jstringToString(env, points_)};
    jsoncons::json val = jsoncons::json::parse(points);
    std::vector<double> leftTop = val[0].as<std::vector<double>>();
    std::vector<double> rightTop = val[1].as<std::vector<double>>();
    std::vector<double> rightBottom = val[2].as<std::vector<double>>();
    std::vector<double> leftBottom = val[3].as<std::vector<double>>();

    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);

    AndroidBitmapInfo outBitmapInfo;
    if (AndroidBitmap_getInfo(env, outBitmap, &outBitmapInfo) < 0) {
        jclass je = env->FindClass("java/lang/Exception");
        env->ThrowNew(je, "AndroidBitmap_getInfo failed for output bitmap");
        return;
    }
    Mat dstBitmapMat;
    int newHeight = (int)outBitmapInfo.height;
    int newWidth = (int)outBitmapInfo.width;
    dstBitmapMat = Mat::zeros(newHeight, newWidth, srcBitmapMat.type());

    std::vector<Point2f> srcTriangle;
    std::vector<Point2f> dstTriangle;

    srcTriangle.emplace_back((float)leftTop[0], (float)leftTop[1]);
    srcTriangle.emplace_back((float)rightTop[0], (float)rightTop[1]);
    srcTriangle.emplace_back((float)leftBottom[0], (float)leftBottom[1]);
    srcTriangle.emplace_back((float)rightBottom[0], (float)rightBottom[1]);

    dstTriangle.emplace_back(0.0f, 0.0f);
    dstTriangle.emplace_back((float)newWidth, 0.0f);
    dstTriangle.emplace_back(0.0f, (float)newHeight);
    dstTriangle.emplace_back((float)newWidth, (float)newHeight);

    Mat transform = getPerspectiveTransform(srcTriangle, dstTriangle);
    warpPerspective(srcBitmapMat, dstBitmapMat, transform, dstBitmapMat.size());
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "native_crop %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());

    std::string transformsStd{jstringToString(env, transforms)};
    if (!transformsStd.empty())
    {
        detector::DocumentDetector::applyTransforms(dstBitmapMat, transformsStd, false);
    }
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "native_crop transform %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
    mat_to_bitmap(env, dstBitmapMat, outBitmap);
    srcBitmapMat.release();
    dstBitmapMat.release();
//    __android_log_print(ANDROID_LOG_INFO,     TAG, "native_crop done %d ms\n", duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count());
}

static jstring native_qrcore_read(JNIEnv *env, jobject type, jobject srcBitmap, jstring options_, jdouble scale)
{
#ifdef  WITH_QRCODE
    Mat srcBitmapMat;
    bitmap_to_mat(env, srcBitmap, srcBitmapMat);
    std::string options{jstringToString(env, options_)};
    string s = readQRCode(srcBitmapMat, 0, options, scale);
    return stringToJavaString(env, s);
#else
    return nullptr;
#endif
}

static jobject generateQRCode(JNIEnv *env, jobject type, jstring text_, jstring format_, jint width, jint height, jstring options_)
{
#ifdef  WITH_QRCODE
    std::string format{jstringToString(env, format_)};
    std::string options{jstringToString(env, options_)};
    std::string text{jstringToString(env, text_)};
    Mat resultMat = generateQRCode(text, format, width, height, options);
    jobject outBitmap = createBitmap(env, resultMat.cols, resultMat.rows);
    mat_to_bitmap(env, resultMat, outBitmap);
    return outBitmap;
#else
    return nullptr;
#endif
}

static jstring native_qrcore_read_buffer(JNIEnv *env, jobject type, jint width, jint height, jint rotation, jobject buffer1,
                                          jint rowStride1, jstring options_)
{
#ifdef  WITH_QRCODE
    void *pixels = (env->GetDirectBufferAddress(buffer1));
    Mat mat(cv::Size(width, height), CV_8UC1, pixels, rowStride1);
    std::string options{jstringToString(env, options_)};
    string s = readQRCode(mat, rotation, options, 1.0f);
    return stringToJavaString(env, s);
#else
    return nullptr;
#endif
}

// extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        return JNI_FALSE;
    }
    initClassInfo(env);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeScan(JNIEnv *env,
                                                                                      jobject thiz,
                                                                                      jobject src_bitmap,
                                                                                      jint shrunk_image_height,
                                                                                      jint image_rotation)
{
    return native_scan(env, thiz, src_bitmap, shrunk_image_height, image_rotation);
}

//extern "C" JNIEXPORT jobject JNICALL
//Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeBufferScan(JNIEnv *env,
//                                                                                      jobject thiz,
//                                                                                            jint width, jint height, jint chromaPixelStride, jobject buffer1,
//                                                                                            jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
//                                                                                            jint rowStride3,
//                                                                                      jint shrunk_image_height,
//                                                                                      jint image_rotation)
//{
//    return native_scan_buffer(env, thiz, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, shrunk_image_height, image_rotation);
//}

// extern "C" JNIEXPORT jstring JNICALL
// Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeScanAndQRCode(JNIEnv *env,
//                                                                                       jobject thiz,
//                                                                                       jobject src_bitmap,
//                                                                                       jint shrunk_image_height,
//                                                                                       jint image_rotation,
//                                                                                       jstring options,
//                                                                                       jdouble scale)
// {
//     return native_scan_qrcode(env, thiz, src_bitmap, shrunk_image_height, image_rotation, options, scale);
// }
extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeScanJSON(JNIEnv *env,
                                                                                          jobject thiz,
                                                                                          jobject src_bitmap,
                                                                                          jint shrunk_image_height,
                                                                                          jint image_rotation,
                                                                                          jdouble scale,
                                                                                          jstring options)
{
    return native_scan_json(env, thiz, src_bitmap, shrunk_image_height, image_rotation, scale, options);
}
//extern "C" JNIEXPORT jstring JNICALL
//Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeScanJSONFromProxy(JNIEnv *env,
//                                                                                          jobject thiz,
//                                                                                        jint width, jint height, jint chromaPixelStride, jobject buffer1,
//                                                                                        jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
//                                                                                        jint rowStride3,
//                                                                                          jint shrunk_image_height,
//                                                                                          jint image_rotation,
//                                                                                                    jobject out_bitmap)
//{
//    return native_scan_json_buffer_with_image(env, thiz, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, shrunk_image_height, image_rotation, out_bitmap);
//}
//extern "C" JNIEXPORT jstring JNICALL
//Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeScanJSONFromProxyOnePlane(JNIEnv *env,
//                                                                                          jobject thiz,
//                                                                                        jint width, jint height, jobject buffer1,
//                                                                                        jint rowStride1,
//                                                                                          jint shrunk_image_height,
//                                                                                          jint image_rotation,
//                                                                                                    jobject out_bitmap)
//{
//    return native_scan_json_buffer_with_image_one_plane(env, thiz, width, height, buffer1, rowStride1, shrunk_image_height, image_rotation, out_bitmap);
//}
extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeColorPalette(JNIEnv *env,
                                                                                          jobject thiz,
                                                                                          jobject src_bitmap,
                                                                                          jint shrunk_image_height,
                                                                                          jint colorsFilterDistanceThreshold,
                                                                                          jint nbColors,
                                                                                          jint colorPalette)
{
    return native_color_palette(env, thiz, src_bitmap, shrunk_image_height, colorsFilterDistanceThreshold, nbColors, colorPalette);
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeBufferScanJSON(JNIEnv *env,
                                                                                          jobject thiz,
                                                                                                jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                                                                                jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                                                                                jint rowStride3,
                                                                                          jint shrunk_image_height,
                                                                                          jint image_rotation)
{
    return native_scan_json_buffer(env, thiz, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, shrunk_image_height, image_rotation);
}

extern "C" JNIEXPORT void JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeCrop(JNIEnv *env,
                                                                                      jobject thiz,
                                                                                      jobject src_bitmap,
                                                                                      jstring points,
                                                                                      jstring transforms,
                                                                                      jobject out_bitmap)
{
    native_crop(env, thiz, src_bitmap, points, transforms, out_bitmap);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeOCR(JNIEnv *env,
                                                                                     jobject thiz,
                                                                                     jobject src_bitmap,
                                                                                     jstring options,
                                                                                     jobject progressInterface)
{
    return native_ocr(env, thiz, src_bitmap, options, progressInterface);
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeQRCodeRead(JNIEnv *env,
                                                                                            jobject thiz,
                                                                                            jobject src_bitmap,
                                                                                            jstring options,
                                                                                            jdouble scale)
{
    return native_qrcore_read(env, thiz, src_bitmap, options, scale);
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeQRCodeReadBuffer(JNIEnv *env,
                                                                                            jobject thiz,
                                                                                                  jint width, jint height, jint rotation, jobject buffer1,
                                                                                                  jint rowStride1,
                                                                                            jstring options)
{
    return native_qrcore_read_buffer(env, thiz, width, height, rotation, buffer1, rowStride1, options);
}


extern "C" JNIEXPORT void JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_testImageProxyToBitmap(JNIEnv *env,
                                                                                                jobject thiz,
                                                                                                jint width, jint height, jint chromaPixelStride, jobject buffer1,
                                                                                                jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                                                                                                jint rowStride3,jobject out_bitmap)
{
    testImageProxyToBitmap(env, thiz, width, height, chromaPixelStride, buffer1, rowStride1, buffer2, rowStride2, buffer3 , rowStride3, out_bitmap);
}
extern "C" JNIEXPORT jobject JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeGenerateQRCode(JNIEnv *env, jobject thiz, jstring text_, jstring format_, jint width, jint height, jstring options_)
{
    return generateQRCode(env, thiz, text_, format_, width, height, options_);
}
extern "C"
JNIEXPORT jstring JNICALL
Java_com_akylas_documentscanner_CustomImageAnalysisCallback_00024Companion_nativeGenerateQRCodeSVG(
        JNIEnv *env, jobject thiz, jstring text_, jstring format_, jint size_hint,
        jstring options_) {
#ifdef  WITH_QRCODE
    std::string format{jstringToString(env, format_)};
    std::string options{jstringToString(env, options_)};
    std::string text{jstringToString(env, text_)};
    std::string result = generateQRCodeSVG(text, format, size_hint, options);
    return stringToJavaString(env, result);
#else
    return nullptr;
#endif
}