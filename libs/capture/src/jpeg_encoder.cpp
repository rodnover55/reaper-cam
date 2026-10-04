#include "cam/capture/jpeg_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio> // jpeglib.h ждёт FILE объявленным до себя

extern "C" {
#include <WDL/jpeglib/jpeglib.h>
}

namespace cam::capture {
namespace {

/// Приёмник jpeglib, который дописывает сжатое в вектор.
struct VectorDestination {
  jpeg_destination_mgr manager{}; // первым полем: jpeglib видит только его
  std::vector<std::uint8_t> *out = nullptr;
  std::array<JOCTET, 16384> buffer{};
};

VectorDestination &destinationOf(j_compress_ptr cinfo) {
  return *reinterpret_cast<VectorDestination *>(cinfo->dest);
}

void initDestination(j_compress_ptr cinfo) {
  auto &dest = destinationOf(cinfo);
  dest.manager.next_output_byte = dest.buffer.data();
  dest.manager.free_in_buffer = dest.buffer.size();
}

boolean emptyOutputBuffer(j_compress_ptr cinfo) {
  auto &dest = destinationOf(cinfo);
  dest.out->insert(dest.out->end(), dest.buffer.begin(), dest.buffer.end());
  dest.manager.next_output_byte = dest.buffer.data();
  dest.manager.free_in_buffer = dest.buffer.size();
  return TRUE;
}

void termDestination(j_compress_ptr cinfo) {
  auto &dest = destinationOf(cinfo);
  const std::size_t used = dest.buffer.size() - dest.manager.free_in_buffer;
  dest.out->insert(dest.out->end(), dest.buffer.begin(),
                   dest.buffer.begin() + static_cast<std::ptrdiff_t>(used));
}

/// По умолчанию jpeglib на ошибке зовёт exit(). Здесь ошибка становится
/// исключением: jpeglib собран с -fexceptions, и раскрутка через его кадры
/// законна (CMakeLists.txt, цель wdl_jpeg).
[[noreturn]] void throwOnError(j_common_ptr cinfo) {
  std::array<char, JMSG_LENGTH_MAX> message{};
  (*cinfo->err->format_message)(cinfo, message.data());
  throw JpegError(message.data());
}

void ignoreMessage(j_common_ptr /*cinfo*/, int /*level*/) {}

/// Освобождает память jpeglib и при выходе по исключению.
struct CompressGuard {
  explicit CompressGuard(jpeg_compress_struct *compress) : cinfo(compress) {}
  ~CompressGuard() { jpeg_destroy_compress(cinfo); }
  CompressGuard(const CompressGuard &) = delete;
  CompressGuard &operator=(const CompressGuard &) = delete;
  CompressGuard(CompressGuard &&) = delete;
  CompressGuard &operator=(CompressGuard &&) = delete;

  jpeg_compress_struct *cinfo;
};

/// Общая часть сжатия: `fillRow(row, y)` кладёт в `row` строку `y` по три
/// байта на точку в цветовом пространстве `space`.
template <typename FillRow>
std::vector<std::uint8_t> compress(int width, int height, J_COLOR_SPACE space, int quality,
                                   FillRow fillRow) {
  std::vector<std::uint8_t> out;

  jpeg_error_mgr errors{};
  jpeg_std_error(&errors);
  errors.error_exit = throwOnError;
  errors.emit_message = ignoreMessage;

  jpeg_compress_struct cinfo{};
  cinfo.err = &errors;
  jpeg_create_compress(&cinfo);
  const CompressGuard guard(&cinfo);

  VectorDestination destination;
  destination.out = &out;
  destination.manager.init_destination = initDestination;
  destination.manager.empty_output_buffer = emptyOutputBuffer;
  destination.manager.term_destination = termDestination;
  cinfo.dest = &destination.manager;

  cinfo.image_width = static_cast<JDIMENSION>(width);
  cinfo.image_height = static_cast<JDIMENSION>(height);
  cinfo.input_components = 3;
  cinfo.in_color_space = space;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  std::vector<JSAMPLE> row(static_cast<std::size_t>(width) * 3);
  while (cinfo.next_scanline < cinfo.image_height) {
    fillRow(std::span<JSAMPLE>(row), static_cast<std::size_t>(cinfo.next_scanline));
    JSAMPLE *rows = row.data();
    jpeg_write_scanlines(&cinfo, &rows, 1);
  }

  jpeg_finish_compress(&cinfo);
  return out;
}

/// Видеодиапазон в полный: яркость 16–235 и цветность 16–240 растягиваются
/// на 0–255, как ждёт JFIF.
std::uint8_t expandLuma(std::uint8_t value) {
  const int full = ((static_cast<int>(value) - 16) * 255 + 109) / 219;
  return static_cast<std::uint8_t>(std::clamp(full, 0, 255));
}

std::uint8_t expandChroma(std::uint8_t value) {
  const int full = 128 + (((static_cast<int>(value) - 128) * 255) / 224);
  return static_cast<std::uint8_t>(std::clamp(full, 0, 255));
}

} // namespace

std::vector<std::uint8_t> compressJpeg(std::span<const std::uint8_t> rgb, int width,
                                       int height, int quality) {
  if (width <= 0 || height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const auto rowBytes = static_cast<std::size_t>(width) * 3;
  if (rgb.size() != rowBytes * static_cast<std::size_t>(height))
    throw JpegError("размер буфера не совпадает с размером картинки");

  return compress(width, height, JCS_RGB, quality, [&](std::span<JSAMPLE> row, std::size_t y) {
    std::ranges::copy(rgb.subspan(rowBytes * y, rowBytes), row.begin());
  });
}

std::vector<std::uint8_t> compressNv12(const Nv12Image &image, int quality) {
  if (image.width <= 0 || image.height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const auto width = static_cast<std::size_t>(image.width);
  const auto height = static_cast<std::size_t>(image.height);
  const std::size_t chromaWidth = (width + 1) / 2 * 2;
  const std::size_t chromaRows = (height + 1) / 2;
  if (image.lumaStride < width || image.chromaStride < chromaWidth ||
      image.luma.size() < (image.lumaStride * (height - 1)) + width ||
      image.chroma.size() < (image.chromaStride * (chromaRows - 1)) + chromaWidth)
    throw JpegError("плоскости кадра меньше его размера");

  // Цветность одна на квадрат 2×2: строка JPEG получает её повторённой.
  return compress(image.width, image.height, JCS_YCbCr, quality,
                  [&](std::span<JSAMPLE> row, std::size_t y) {
                    const auto luma = image.luma.subspan(image.lumaStride * y, width);
                    const auto chroma =
                        image.chroma.subspan(image.chromaStride * (y / 2), chromaWidth);
                    for (std::size_t x = 0; x < width; ++x) {
                      const std::uint8_t cb = chroma[(x / 2) * 2];
                      const std::uint8_t cr = chroma[((x / 2) * 2) + 1];
                      row[(x * 3)] = image.videoRange ? expandLuma(luma[x]) : luma[x];
                      row[(x * 3) + 1] = image.videoRange ? expandChroma(cb) : cb;
                      row[(x * 3) + 2] = image.videoRange ? expandChroma(cr) : cr;
                    }
                  });
}

std::vector<std::uint8_t> compressYuy2(const Yuy2Image &image, int quality) {
  if (image.width <= 0 || image.height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const auto width = static_cast<std::size_t>(image.width);
  const auto height = static_cast<std::size_t>(image.height);
  const std::size_t rowBytes = (width + 1) / 2 * 4;
  if (image.stride < rowBytes || image.data.size() < (image.stride * (height - 1)) + rowBytes)
    throw JpegError("кадр меньше его размера");

  return compress(image.width, image.height, JCS_YCbCr, quality,
                  [&](std::span<JSAMPLE> row, std::size_t y) {
                    const auto source = image.data.subspan(image.stride * y, rowBytes);
                    for (std::size_t x = 0; x < width; ++x) {
                      const std::size_t pair = (x / 2) * 4;
                      const std::uint8_t luma = source[pair + ((x % 2) * 2)];
                      const std::uint8_t cb = source[pair + 1];
                      const std::uint8_t cr = source[pair + 3];
                      row[(x * 3)] = image.videoRange ? expandLuma(luma) : luma;
                      row[(x * 3) + 1] = image.videoRange ? expandChroma(cb) : cb;
                      row[(x * 3) + 2] = image.videoRange ? expandChroma(cr) : cr;
                    }
                  });
}

std::vector<std::uint8_t> compressBlackFrame(int width, int height) {
  if (width <= 0 || height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const std::vector<std::uint8_t> black(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0);
  return compressJpeg(black, width, height, 75);
}

} // namespace cam::capture
