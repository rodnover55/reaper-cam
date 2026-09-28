#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cam::capture {

/// Сжатие картинки в JPEG. Кадры камеры не сжимаются никогда: они пишутся как
/// пришли (design.md D7). Сжимаются только кадры тестового источника и
/// чёрный кадр, один раз на режим.
///
/// `rgb` — строки подряд, по три байта на точку (красный, зелёный, синий),
/// без выравнивания строк. Качество — от 1 до 100, как у jpeglib.
std::vector<std::uint8_t> compressJpeg(std::span<const std::uint8_t> rgb, int width,
                                       int height, int quality);

/// Чёрный кадр размера режима камеры. Место сетки получает его, когда камеры
/// нет или она пропала (design.md D4, D11). Сжимается один раз при открытии
/// режима: хранит его тот, кто режим открыл.
std::vector<std::uint8_t> compressBlackFrame(int width, int height);

/// Ошибка сжатия: неверные размеры или сбой внутри jpeglib.
class JpegError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace cam::capture
