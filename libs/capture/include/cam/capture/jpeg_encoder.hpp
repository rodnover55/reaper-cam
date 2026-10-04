#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cam::capture {

/// Сжатие картинки в JPEG. Кадры камеры с MJPEG пишутся как пришли
/// (design.md D7); сжимаются кадры тестового источника, чёрный кадр (один раз
/// на режим) и кадры камер, которые MJPEG не отдают (compressNv12).
///
/// `rgb` — строки подряд, по три байта на точку (красный, зелёный, синий),
/// без выравнивания строк. Качество — от 1 до 100, как у jpeglib.
std::vector<std::uint8_t> compressJpeg(std::span<const std::uint8_t> rgb, int width,
                                       int height, int quality);

/// Качество JPEG для кадров камер без MJPEG, которые сжимает захват: от 1 до
/// 100, как у jpeglib. При 85 заметной разницы с оригиналом нет.
inline constexpr int kCameraJpegQuality = 85;

/// Кадр NV12 — так отдают картинку камеры без MJPEG (встроенные камеры Mac,
/// многие камеры ноутбуков): яркость плоскостью, под ней цветность Cb Cr
/// парами, одна пара на квадрат 2×2 точки.
struct Nv12Image {
  int width = 0;
  int height = 0;
  std::span<const std::uint8_t> luma;
  std::size_t lumaStride = 0;
  std::span<const std::uint8_t> chroma;
  std::size_t chromaStride = 0;
  /// Видеодиапазон (яркость 16–235): так отдают камеры. JPEG ждёт полный, и
  /// значения растягиваются.
  bool videoRange = true;
};

/// Сжимает кадр NV12 в JPEG программно. Запасной путь: там, где у системы
/// есть свой кодер JPEG, захват сжимает им.
std::vector<std::uint8_t> compressNv12(const Nv12Image &image, int quality);

/// Кадр YUY2 (YUYV): точки парами, на пару четыре байта — Y0 Cb Y1 Cr. Так
/// отдают картинку многие камеры USB без MJPEG и часть встроенных.
struct Yuy2Image {
  int width = 0;
  int height = 0;
  std::span<const std::uint8_t> data;
  std::size_t stride = 0;
  bool videoRange = true;
};

/// Сжимает кадр YUY2 в JPEG программно.
std::vector<std::uint8_t> compressYuy2(const Yuy2Image &image, int quality);

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
