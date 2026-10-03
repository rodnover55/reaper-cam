#include "frame_compressor.hpp"

#include "cam/capture/jpeg_encoder.hpp"
#include "cam/capture_avf/jpeg_compressor.hpp"

#import <Foundation/Foundation.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <span>

namespace cam::capture_avf::detail {

FrameCompressor::FrameCompressor(int quality) : quality_(quality) {}

FrameCompressor::~FrameCompressor() { dropSession(); }

void FrameCompressor::dropSession() {
  if (session_ == nullptr)
    return;
  VTCompressionSessionInvalidate(session_);
  CFRelease(session_);
  session_ = nullptr;
}

std::optional<std::vector<std::uint8_t>> FrameCompressor::compress(CVPixelBufferRef pixels) {
  if (!videoToolboxFailed_) {
    if (auto jpeg = withVideoToolbox(pixels)) {
      ++byVideoToolbox_;
      return jpeg;
    }
    // Кодер отказал раз — дальше без него: пробовать на каждом кадре дорого.
    videoToolboxFailed_ = true;
    dropSession();
  }

  if (auto jpeg = withJpeglib(pixels)) {
    ++byJpeglib_;
    return jpeg;
  }
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>>
FrameCompressor::withVideoToolbox(CVPixelBufferRef pixels) {
  const auto width = static_cast<int>(CVPixelBufferGetWidth(pixels));
  const auto height = static_cast<int>(CVPixelBufferGetHeight(pixels));

  if (session_ != nullptr && (width != sessionWidth_ || height != sessionHeight_))
    dropSession();

  if (session_ == nullptr) {
    if (VTCompressionSessionCreate(kCFAllocatorDefault, width, height, kCMVideoCodecType_JPEG,
                                   nullptr, nullptr, nullptr, nullptr, nullptr,
                                   &session_) != noErr) {
      session_ = nullptr;
      return std::nullopt;
    }
    sessionWidth_ = width;
    sessionHeight_ = height;

    const double quality = static_cast<double>(quality_) / 100.0;
    VTSessionSetProperty(session_, kVTCompressionPropertyKey_Quality,
                         (__bridge CFNumberRef)[NSNumber numberWithDouble:quality]);
    VTSessionSetProperty(session_, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
  }

  // Обработчик зовётся до возврата VTCompressionSessionCompleteFrames, поэтому
  // указатель на локальный вектор в нём законен.
  std::vector<std::uint8_t> jpeg;
  std::vector<std::uint8_t> *out = &jpeg;
  const OSStatus status = VTCompressionSessionEncodeFrameWithOutputHandler(
      session_, pixels, CMTimeMake(encoded_++, 30), kCMTimeInvalid, nullptr, nullptr,
      ^(OSStatus result, VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
        (void)flags;
        if (result != noErr || sample == nullptr)
          return;
        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        if (block == nullptr)
          return;
        const std::size_t length = CMBlockBufferGetDataLength(block);
        out->resize(length);
        if (CMBlockBufferCopyDataBytes(block, 0, length, out->data()) != kCMBlockBufferNoErr)
          out->clear();
      });
  if (status != noErr)
    return std::nullopt;

  VTCompressionSessionCompleteFrames(session_, kCMTimeInvalid);
  if (jpeg.empty())
    return std::nullopt;
  return jpeg;
}

std::optional<std::vector<std::uint8_t>>
FrameCompressor::withJpeglib(CVPixelBufferRef pixels) const {
  const OSType format = CVPixelBufferGetPixelFormatType(pixels);
  const bool videoRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  if (!videoRange && format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
    return std::nullopt;

  if (CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
    return std::nullopt;

  std::optional<std::vector<std::uint8_t>> jpeg;
  try {
    const std::size_t height = CVPixelBufferGetHeight(pixels);
    const std::size_t lumaStride = CVPixelBufferGetBytesPerRowOfPlane(pixels, 0);
    const std::size_t chromaStride = CVPixelBufferGetBytesPerRowOfPlane(pixels, 1);
    const std::size_t chromaRows = CVPixelBufferGetHeightOfPlane(pixels, 1);
    const auto *luma =
        static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixels, 0));
    const auto *chroma =
        static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixels, 1));

    if (luma != nullptr && chroma != nullptr) {
      const capture::Nv12Image image{
          .width = static_cast<int>(CVPixelBufferGetWidth(pixels)),
          .height = static_cast<int>(height),
          .luma = std::span<const std::uint8_t>(luma, lumaStride * height),
          .lumaStride = lumaStride,
          .chroma = std::span<const std::uint8_t>(chroma, chromaStride * chromaRows),
          .chromaStride = chromaStride,
          .videoRange = videoRange};
      jpeg = capture::compressNv12(image, quality_);
    }
  } catch (const std::exception &) {
    jpeg.reset();
  }

  CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
  return jpeg;
}

std::string FrameCompressor::describe() const {
  return std::format("compressed by VideoToolbox {}, by jpeglib {}", byVideoToolbox_.load(),
                     byJpeglib_.load());
}

} // namespace cam::capture_avf::detail

namespace cam::capture_avf {

std::optional<std::vector<std::uint8_t>>
compressNv12WithVideoToolbox(const capture::Nv12Image &image, int quality) {
  if (image.width <= 0 || image.height <= 0)
    return std::nullopt;

  const OSType format = image.videoRange ? kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
                                         : kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
  NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey : @{}};

  CVPixelBufferRef pixels = nullptr;
  if (CVPixelBufferCreate(kCFAllocatorDefault, static_cast<std::size_t>(image.width),
                          static_cast<std::size_t>(image.height), format,
                          (__bridge CFDictionaryRef)attributes, &pixels) != kCVReturnSuccess)
    return std::nullopt;

  CVPixelBufferLockBaseAddress(pixels, 0);
  const auto copyPlane = [&](std::size_t plane, std::span<const std::uint8_t> from,
                             std::size_t fromStride, std::size_t rowBytes) {
    auto *to = static_cast<std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixels, plane));
    const std::size_t toStride = CVPixelBufferGetBytesPerRowOfPlane(pixels, plane);
    const std::size_t rows = CVPixelBufferGetHeightOfPlane(pixels, plane);
    for (std::size_t row = 0; row < rows; ++row)
      std::memcpy(to + (row * toStride), from.data() + (row * fromStride), rowBytes);
  };
  const auto width = static_cast<std::size_t>(image.width);
  copyPlane(0, image.luma, image.lumaStride, width);
  copyPlane(1, image.chroma, image.chromaStride, (width + 1) / 2 * 2);
  CVPixelBufferUnlockBaseAddress(pixels, 0);

  CVBufferSetAttachment(pixels, kCVImageBufferYCbCrMatrixKey,
                        kCVImageBufferYCbCrMatrix_ITU_R_601_4,
                        kCVAttachmentMode_ShouldPropagate);

  detail::FrameCompressor compressor(quality);
  std::optional<std::vector<std::uint8_t>> jpeg;
  @autoreleasepool {
    jpeg = compressor.withVideoToolbox(pixels);
  }
  CVPixelBufferRelease(pixels);
  return jpeg;
}

} // namespace cam::capture_avf
