#ifdef  WITH_QRCODE
#include "./include/QRCode.h"

#ifdef __APPLE__
#include <ZXingCpp/BitMatrix.h>
#include <ZXingCpp/Barcode.h>
#include <ZXingCpp/WriteBarcode.h>
#else
#include <BitMatrix.h>
#include <Barcode.h>
#include <WriteBarcode.h>
#endif
#include <regex>

using namespace std;

ZXing::ImageView ImageViewFromMat(const cv::Mat& image)
{
  using ZXing::ImageFormat;
  
  auto fmt = ImageFormat::None;
  switch (image.channels()) {
    case 1: fmt = ImageFormat::Lum; break;
    case 3: fmt = ImageFormat::BGR; break;
    case 4: fmt = ImageFormat::BGRX; break;
  }
  
  if (image.depth() != CV_8U || fmt == ImageFormat::None)
    return {nullptr, 0, 0, ImageFormat::None};
  
  return {image.data, image.cols, image.rows, fmt};
}



namespace jsoncons {
template<class Json>
struct json_type_traits<Json, ZXing::Position>
{
  using allocator_type = typename Json::allocator_type;
  static Json to_json(const ZXing::Position& val,
                      allocator_type allocator=allocator_type())
  {
    Json j(json_array_arg);
    Json j1(json_array_arg);
    j1.push_back( val.topLeft().x);
    j1.push_back( val.topLeft().y);
    j.push_back(j1);
    j1.clear();
    j1.push_back( val.topRight().x);
    j1.push_back( val.topRight().y);
    j.push_back(j1);
    j1.clear();
    j1.push_back( val.bottomRight().x);
    j1.push_back( val.bottomRight().y);
    j.push_back(j1);
    j1.clear();
    j1.push_back( val.bottomLeft().x);
    j1.push_back( val.bottomLeft().y);
    j.push_back(j1);
    return j;
  }
};
} // namespace jsoncons


std::string readQRCode(cv::Mat &srcMat, int jRotation, string options, double scale)
{
  auto hints = ZXing::DecodeHints().setFormats(ZXing::BarcodeFormat::Any).setMaxNumberOfSymbols(1);
  int rotation = jRotation;
  if (!options.empty()) {
    jsoncons::json j = jsoncons::json::parse(options);
    
    if (j.contains("tryHarder")) {
      hints = hints.setTryHarder(j["tryHarder"].as<bool>());
    }
    if (j.contains("rotation")) {
      rotation = j["rotation"].as<int>();
    }
    if (j.contains("resizeThreshold")) {
      double resizeThreshold = j["resizeThreshold"].as<double>();
      int width = srcMat.cols;
      int height = srcMat.rows;
      int size = max(width, height);
      if (size > resizeThreshold) {
        scale = 1.0f * size / resizeThreshold;
        cv::Size size(width / scale, height / scale);
        cv::Mat resizedBitmap(size, CV_8UC3);
        resize(srcMat, resizedBitmap, size);
        srcMat.release();
        srcMat  = resizedBitmap;
      }
    }
  }
  ZXing::Results results = ZXing::ReadBarcodes(ImageViewFromMat(srcMat).rotated(rotation), hints);
  for (int i = 0; i < results.size(); ++i) {
    auto position = results[i].position();
    auto topLeft = position.topLeft();
    auto topRight = position.topRight();
    auto bottomRight = position.bottomRight();
    auto bottomLeft = position.bottomLeft();
    results[i].setPosition(ZXing::QuadrilateralI(ZXing::PointI(topLeft.x * scale, topLeft.y * scale),
                                                 ZXing::PointI(topRight.x * scale, topRight.y * scale),
                                                 ZXing::PointI(bottomRight.x * scale, bottomRight.y * scale),
                                                 ZXing::PointI(bottomLeft.x * scale, bottomLeft.y * scale)));
  }
  
  string s;
  encode_json(results, s, jsoncons::indenting::no_indent);
  return s;
}

cv::Mat generateQRCode(std::string text, std::string format, int width, int height, std::string options)
{
  //    auto writer = ZXing::MultiFormatWriter(ZXing::BarcodeFormatFromString(format));
  //
  auto cOpts = ZXing::CreatorOptions(ZXing::BarcodeFormatFromString(format));
  auto wOpts = ZXing::WriterOptions().sizeHint(max(width, height));
  //
  if (!options.empty()) {
    jsoncons::json j = jsoncons::json::parse(options);
    
    //        if (j.contains("encoding")) {
    //            wOpts = wOpts.setEncoding(ZXing::CharacterSetFromString(j["encoding"].as<string>()));
    //        }
    if (j.contains("ecclevel")) {
      cOpts.ecLevel(j["ecclevel"].as<string>());
    }
    //        if (j.contains("margin")) {
    //            wOpts = wOpts.setMargin(j["margin"].as<int>());
    //        }
  }
  auto bcode = CreateBarcodeFromText(text, cOpts);
  auto image = WriteBarcodeToImage(bcode, wOpts);
  const unsigned char *buffer = image.data();
  // Clone immediately: image is a local variable whose data pointer becomes
  // dangling once the function returns. The clone gives resultMat its own copy.
  cv::Mat resultMat = cv::Mat(image.height(), image.width(), CV_8UC1,
                              (unsigned char*)buffer).clone();
  return resultMat;
  
  //    auto writer = ZXing::MultiFormatWriter(ZXing::BarcodeFormatFromString(format));
  //    if (!options.empty()) {
  //        jsoncons::json j = jsoncons::json::parse(options);
  //
  //        if (j.contains("encoding")) {
  //            writer = writer.setEncoding(ZXing::CharacterSetFromString(j["encoding"].as<string>()));
  //        }
  //        if (j.contains("ecclevel")) {
  //            writer = writer.setEccLevel(j["ecclevel"].as<int>());
  //        }
  //        if (j.contains("margin")) {
  //            writer = writer.setMargin(j["margin"].as<int>());
  //        }
  //    }
  //    auto matrix = writer.encode(text, width, height);
  //    auto bitmap = ToMatrix<uint8_t>(matrix);
  //    const unsigned char *buffer = bitmap.data();
  //    cv::Mat resultMat(height,width,CV_8UC1,(unsigned char*)buffer);
  //    return resultMat;
}
bool replace(std::string& str, const std::string& from, const std::string& to) {
  size_t start_pos = str.find(from);
  if(start_pos == std::string::npos)
    return false;
  str.replace(start_pos, from.length(), to);
  return true;
}
std::string prepareSVGCode(std::string code, jsoncons::json j, bool useFallbackColor) {
  replace(code, "fill=\"#FFFFFF\"", "fill=\"transparent\"");
  if (j != nullptr) {
    std::string color;
    if (useFallbackColor) {
      color = j["fallbackColor"].as<string>();
    } else {
      color = j["color"].as<string>();
    }
    if (!color.empty()) {
      if (code.find("fill=") != std::string::npos) {
        replace(code, "fill=\"#000000\"", "fill=\"" + color + "\"");
      } else {
        replace(code, "<path d", "<path fill=\"" + color + "\" d");
      }
    }
  }
  code = regex_replace(code, std::regex("width=\"(\\d*?)\" height=\"(\\d*?)\""), "width=\"100%\" height=\"100%\" viewBox=\"0 0 $1 $2\"");
  return code;
}
std::string generateQRCodeSVG(std::string text, std::string format, int size_hint, std::string options)
{
  jsoncons::json j = nullptr;
  if (!options.empty()) {
    j = jsoncons::json::parse(options);
  }
  auto cOpts = ZXing::CreatorOptions(ZXing::BarcodeFormatFromString(format));
  auto wOpts = ZXing::WriterOptions().sizeHint(size_hint);
  try {
    //
    //
    if (j != nullptr) {
      
      //        if (j.contains("encoding")) {
      //            wOpts = wOpts.setEncoding(ZXing::CharacterSetFromString(j["encoding"].as<string>()));
      //        }
      if (j.contains("ecclevel")) {
        cOpts.ecLevel(j["ecclevel"].as<string>());
      }
      //        if (j.contains("margin")) {
      //            wOpts = wOpts.setMargin(j["margin"].as<int>());
      //        }
    }
    if (text.empty()) {
      throw std::runtime_error("error");
    }
    std::string result = WriteBarcodeToSVG(CreateBarcodeFromText(text, cOpts), wOpts);
    return prepareSVGCode(result, j, false);
  } catch(const std::exception&) {
    if (j != nullptr && j.contains("fallbackText")) {
      try {
        std::string result = WriteBarcodeToSVG(CreateBarcodeFromText(j["fallbackText"].as<string>(), cOpts), wOpts);
        return prepareSVGCode(result, j, true);
      } catch(const std::exception&) {
        return "";
      }
    }
    return "";
  }
}

#endif
