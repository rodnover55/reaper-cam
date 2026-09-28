#include "cam/capture/jpeg_encoder.hpp"

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

} // namespace

std::vector<std::uint8_t> compressJpeg(std::span<const std::uint8_t> rgb, int width,
                                       int height, int quality) {
  if (width <= 0 || height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const auto rowBytes = static_cast<std::size_t>(width) * 3;
  if (rgb.size() != rowBytes * static_cast<std::size_t>(height))
    throw JpegError("размер буфера не совпадает с размером картинки");

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
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height) {
    // jpeglib не пишет во входную строку, но объявляет её неконстантной.
    auto *row = const_cast<JSAMPLE *>(rgb.data() + (rowBytes * cinfo.next_scanline));
    jpeg_write_scanlines(&cinfo, &row, 1);
  }

  jpeg_finish_compress(&cinfo);
  return out;
}

std::vector<std::uint8_t> compressBlackFrame(int width, int height) {
  if (width <= 0 || height <= 0)
    throw JpegError("размер картинки должен быть положительным");

  const std::vector<std::uint8_t> black(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0);
  return compressJpeg(black, width, height, 75);
}

} // namespace cam::capture
