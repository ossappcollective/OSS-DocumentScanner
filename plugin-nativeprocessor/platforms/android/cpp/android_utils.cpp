#include "android_utils.h"


void buffer_to_mat(JNIEnv *env, jint width, jint height, jint chromaPixelStride, jobject buffer1,
                   jint rowStride1, jobject buffer2, jint rowStride2, jobject buffer3,
                   jint rowStride3, Mat &srcMat) {
    if (chromaPixelStride == 2) { // Chroma channels are interleaved
        void *yPlane = (env->GetDirectBufferAddress(buffer1));
        int yPlaneStep = rowStride1;
        void *uvPlane1 = (env->GetDirectBufferAddress(buffer2));
        int uvPlane1Step = rowStride2;
        void *uvPlane2 = (env->GetDirectBufferAddress(buffer3));
        int uvPlane2Step = rowStride3;
        Mat yMat(cv::Size(width, height), CV_8UC1, yPlane, yPlaneStep);
        Mat uvMat1(cv::Size(width / 2, height / 2), CV_8UC2, uvPlane1, uvPlane1Step);
        Mat uvMat2(cv::Size(width / 2, height / 2), CV_8UC2, uvPlane2, uvPlane2Step);
        ptrdiff_t addrDiff = uvMat2.data - uvMat1.data;
        if (addrDiff > 0) {
            uvMat2.release();
            cv::cvtColorTwoPlane(yMat, uvMat1, srcMat, cv::COLOR_YUV2RGBA_NV12);
            yMat.release();
            uvMat1.release();
        } else {
            uvMat1.release();
            cv::cvtColorTwoPlane(yMat, uvMat2, srcMat, cv::COLOR_YUV2RGBA_NV21);
            yMat.release();
            uvMat2.release();
        }
    } else { // Chroma channels are not interleaved
        // Allocate memory for the YUV frame (size_t cast avoids signed-integer overflow)
        char *yuvBytes = new char[static_cast<size_t>(width) * (height + height / 2)];
        // Get the pointers to the Y, U, and V planes of the YUV frame
        const uint8_t *yPlane = static_cast<uint8_t *>(env->GetDirectBufferAddress(buffer1));
        const uint8_t *uPlane = static_cast<uint8_t *>(env->GetDirectBufferAddress(buffer2));
        const uint8_t *vPlane = static_cast<uint8_t *>(env->GetDirectBufferAddress(buffer3));
        // Offset for the bytes in the Y plane
        int yuvBytesOffset = 0;
        // Row stride of the Y plane
        int yPlaneStep = rowStride1;
        // Number of pixels in the frame
        int pixels = width * height;
        if (yPlaneStep == width) {
            // No row padding: copy the whole Y plane in one shot
            std::copy(yPlane, yPlane + pixels, yuvBytes);
            yuvBytesOffset += pixels;
        } else {
            // Row padding present: copy each Y row individually, advancing by the
            // full row stride so padding bytes are skipped.
            for (int i = 0; i < height; ++i) {
                std::copy(yPlane, yPlane + width, yuvBytes + yuvBytesOffset);
                yuvBytesOffset += width;
                yPlane += yPlaneStep;
            }
        }
        // Copy U and V chroma planes using their own row strides (rowStride2 / rowStride3).
        if (rowStride2 == width / 2 && rowStride3 == width / 2) {
            // No chroma padding: copy each plane in one shot
            std::copy(uPlane, uPlane + pixels / 4, yuvBytes + yuvBytesOffset);
            yuvBytesOffset += pixels / 4;
            std::copy(vPlane, vPlane + pixels / 4, yuvBytes + yuvBytesOffset);
            yuvBytesOffset += pixels / 4;
        } else {
            // Chroma padding present: copy each row individually
            for (int i = 0; i < height / 2; ++i) {
                std::copy(uPlane, uPlane + width / 2, yuvBytes + yuvBytesOffset);
                yuvBytesOffset += width / 2;
                uPlane += rowStride2;
            }
            for (int i = 0; i < height / 2; ++i) {
                std::copy(vPlane, vPlane + width / 2, yuvBytes + yuvBytesOffset);
                yuvBytesOffset += width / 2;
                vPlane += rowStride3;
            }
        }
        Mat yuvMat(cv::Size(width, height + height / 2), CV_8UC1, yuvBytes);
//        std::copy(yuvBytes, yuvBytes + (width * (height + height / 2)), yuvMat.data);

//        memcpy(yuvMat.data, yuvBytes, width * (height + height / 2));
//        yuvMat.put(0, 0, yuvBytes);
        cv::cvtColor(yuvMat, srcMat, cv::COLOR_YUV2RGBA_I420, 4);
        delete[] yuvBytes;
        yuvMat.release();
    }
}

void bitmap_to_mat(JNIEnv *env, jobject &srcBitmap, Mat &srcMat) {
    void *srcPixels = 0;
    AndroidBitmapInfo srcBitmapInfo;
    try {
        if (AndroidBitmap_getInfo(env, srcBitmap, &srcBitmapInfo) < 0 ||
            AndroidBitmap_lockPixels(env, srcBitmap, &srcPixels) < 0 ||
            srcPixels == nullptr) {
            jclass je = env->FindClass("java/lang/Exception");
            env->ThrowNew(je, "Failed to access bitmap pixels");
            return;
        }
        uint32_t srcHeight = srcBitmapInfo.height;
        uint32_t srcWidth = srcBitmapInfo.width;
        srcMat.create(srcHeight, srcWidth, CV_8UC4);
        if (srcBitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
            Mat tmp(srcHeight, srcWidth, CV_8UC4, srcPixels);
            tmp.copyTo(srcMat);
        } else {
            Mat tmp = Mat(srcHeight, srcWidth, CV_8UC2, srcPixels);
            cvtColor(tmp, srcMat, COLOR_BGR5652RGBA);
        }
        AndroidBitmap_unlockPixels(env, srcBitmap);
        return;
//    } catch (cv::Exception &e) {
//        AndroidBitmap_unlockPixels(env, srcBitmap);
//        jclass je = env->FindClass("java/lang/Exception");
//        env->ThrowNew(je, e.what());
//        return;
    } catch (...) {
        AndroidBitmap_unlockPixels(env, srcBitmap);
        jclass je = env->FindClass("java/lang/Exception");
        env->ThrowNew(je, "unknown");
        return;
    }
}

void mat_to_bitmap(JNIEnv *env, Mat &srcMat, jobject &dstBitmap) {
    void *dstPixels = 0;
    AndroidBitmapInfo dstBitmapInfo;
    try {
        if (AndroidBitmap_getInfo(env, dstBitmap, &dstBitmapInfo) < 0 ||
            AndroidBitmap_lockPixels(env, dstBitmap, &dstPixels) < 0 ||
            dstPixels == nullptr) {
            jclass je = env->FindClass("java/lang/Exception");
            env->ThrowNew(je, "Failed to access bitmap pixels");
            return;
        }
        uint32_t dstHeight = dstBitmapInfo.height;
        uint32_t dstWidth = dstBitmapInfo.width;
        if (dstBitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
            Mat tmp(dstHeight, dstWidth, CV_8UC4, dstPixels);
            if (srcMat.type() == CV_8UC1) {
                cvtColor(srcMat, tmp, COLOR_GRAY2RGBA);
            } else if (srcMat.type() == CV_8UC3) {
                cvtColor(srcMat, tmp, COLOR_RGB2RGBA);
            } else if (srcMat.type() == CV_8UC4) {
                srcMat.copyTo(tmp);
            }
        } else {
            Mat tmp = Mat(dstHeight, dstWidth, CV_8UC2, dstPixels);
            if (srcMat.type() == CV_8UC1) {
                cvtColor(srcMat, tmp, COLOR_GRAY2BGR565);
            } else if (srcMat.type() == CV_8UC3) {
                cvtColor(srcMat, tmp, COLOR_RGB2BGR565);
            } else if (srcMat.type() == CV_8UC4) {
                cvtColor(srcMat, tmp, COLOR_RGBA2BGR565);
            }
        }
        AndroidBitmap_unlockPixels(env, dstBitmap);
//    } catch (cv::Exception &e) {
//        AndroidBitmap_unlockPixels(env, dstBitmap);
//        jclass je = env->FindClass("java/lang/Exception");
//        env->ThrowNew(je, e.what());
//        return;
    } catch (...) {
        AndroidBitmap_unlockPixels(env, dstBitmap);
        jclass je = env->FindClass("java/lang/Exception");
        env->ThrowNew(je, "unknown");
        return;
    }
}



