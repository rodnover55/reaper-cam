#pragma once

// Сжатие несжатых кадров камеры в JPEG (только для исходников Objective-C++
// этой библиотеки: в интерфейсе типы CoreVideo).
//
// Сначала — VideoToolbox. Если сеанс кодера не создаётся или кадр не
// сжимается, кадры NV12 сжимает jpeglib (capture::compressNv12); кадры
// другого формата без VideoToolbox не сжать.

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cam::capture_avf::detail {

class FrameCompressor {
public:
  explicit FrameCompressor(int quality);
  ~FrameCompressor();

  FrameCompressor(const FrameCompressor &) = delete;
  FrameCompressor &operator=(const FrameCompressor &) = delete;
  FrameCompressor(FrameCompressor &&) = delete;
  FrameCompressor &operator=(FrameCompressor &&) = delete;

  /// Сжатый кадр; пусто, если не сжал ни один кодер.
  std::optional<std::vector<std::uint8_t>> compress(CVPixelBufferRef pixels);

  /// Только VideoToolbox, без запасного пути.
  std::optional<std::vector<std::uint8_t>> withVideoToolbox(CVPixelBufferRef pixels);

  /// Для журнала: каким кодером сжаты кадры.
  std::string describe() const;

private:
  std::optional<std::vector<std::uint8_t>> withJpeglib(CVPixelBufferRef pixels) const;
  void dropSession();

  int quality_;
  VTCompressionSessionRef session_ = nullptr;
  int sessionWidth_ = 0;
  int sessionHeight_ = 0;
  /// Номер кадра для метки, которую ждёт кодер: метки растут.
  std::int64_t encoded_ = 0;
  bool videoToolboxFailed_ = false;
  // Пишет очередь кадров, читает журнал из другого потока.
  std::atomic<std::uint64_t> byVideoToolbox_{0};
  std::atomic<std::uint64_t> byJpeglib_{0};
};

} // namespace cam::capture_avf::detail
