#pragma once

// Сжатие кадров в JPEG кодером системы — VideoToolbox. Нужно камерам, которые
// MJPEG не отдают: встроенным камерам Mac, iPhone как камере. На Apple Silicon
// и Intel с T2 кодер аппаратный, процессор почти не занят. Заголовок на чистом
// C++: его включают тесты.

#include "cam/capture/jpeg_encoder.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cam::capture_avf {

inline constexpr int kJpegQuality = capture::kCameraJpegQuality;

/// Сжимает кадр NV12 кодером VideoToolbox. Цвета считаются по BT.601, как у
/// JFIF. Пусто, если кодер недоступен или отказал: тогда сжимает
/// capture::compressNv12.
std::optional<std::vector<std::uint8_t>>
compressNv12WithVideoToolbox(const capture::Nv12Image &image, int quality);

} // namespace cam::capture_avf
