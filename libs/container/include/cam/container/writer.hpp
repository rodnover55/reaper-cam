#pragma once

#include "cam/container/output.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace cam::container {

/// Видео в файле: размер кадра и частота кадров дробью, например 30/1 или
/// 30000/1001. Частота постоянная (design.md D4).
struct VideoFormat {
  int width = 0;
  int height = 0;
  int rateNumerator = 30;
  int rateDenominator = 1;
};

/// Писатель контейнера с одной дорожкой MJPEG (design.md D7): кадр номер k
/// стоит в момент k · rateDenominator / rateNumerator от начала файла.
///
/// Контейнер — фрагментированный QuickTime с кодеком Photo-JPEG, фрагмент —
/// секунда: после аварии файл читается до последнего целого фрагмента.
/// Писатель закрывает файл и сам, при уничтожении.
class VideoWriter {
public:
  virtual ~VideoWriter() = default;

  VideoWriter() = default;
  VideoWriter(const VideoWriter &) = delete;
  VideoWriter &operator=(const VideoWriter &) = delete;
  VideoWriter(VideoWriter &&) = delete;
  VideoWriter &operator=(VideoWriter &&) = delete;

  /// Дописывает следующий кадр — JPEG как есть.
  virtual void append(std::span<const std::uint8_t> jpeg) = 0;

  /// Дописывает последний фрагмент. После этого кадров нет.
  virtual void finish() = 0;

  virtual std::uint64_t frameCount() const = 0;
};

std::unique_ptr<VideoWriter> createWriter(std::unique_ptr<Output> output,
                                          const VideoFormat &format);

} // namespace cam::container
