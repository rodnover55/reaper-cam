#include <doctest/doctest.h>

#include "cam/capture/jpeg_encoder.hpp"
#include "jpeg_decode.hpp"
#include "nv12_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using cam::capture::compressNv12;
using cam::capture::compressYuy2;
using cam::capture::JpegError;
using cam::tests::decodeJpeg;
using cam::tests::fillStripes;
using cam::tests::kStripes;
using cam::tests::Nv12Frame;
using cam::tests::Yuy2Frame;

namespace {

/// Разжатый кадр совпадает с полосами: середина каждой полосы своего цвета.
/// Допуск — на потери JPEG и округления пересчёта цвета.
void checkStripes(const std::vector<std::uint8_t> &jpeg, int width, int height) {
  const auto image = decodeJpeg(jpeg);
  REQUIRE(image);
  CHECK(image->width == width);
  CHECK(image->height == height);

  const auto y = static_cast<std::size_t>(height / 2);
  for (std::size_t stripe = 0; stripe < kStripes.size(); ++stripe) {
    CAPTURE(stripe);
    const auto x =
        static_cast<std::size_t>((width * ((2 * static_cast<int>(stripe)) + 1)) / 8);
    const std::size_t at = ((y * static_cast<std::size_t>(width)) + x) * 3;
    CHECK(std::abs(image->rgb[at] - kStripes[stripe].rgb.red) < 24);
    CHECK(std::abs(image->rgb[at + 1] - kStripes[stripe].rgb.green) < 24);
    CHECK(std::abs(image->rgb[at + 2] - kStripes[stripe].rgb.blue) < 24);
  }
}

} // namespace

TEST_CASE("NV12: кадр камеры без MJPEG сжимается в JPEG своих цветов и размера") {
  Nv12Frame frame;
  fillStripes(frame, 128, 64);
  checkStripes(compressNv12(frame.image, 90), 128, 64);
}

TEST_CASE("NV12: строки с выравниванием и нечётный размер") {
  Nv12Frame frame;
  fillStripes(frame, 129, 65, 15);
  checkStripes(compressNv12(frame.image, 90), 129, 65);
}

TEST_CASE("NV12: плоскости меньше заявленного размера — ошибка, а не чтение за краем") {
  Nv12Frame frame;
  fillStripes(frame, 64, 32);
  frame.image.height = 64;
  CHECK_THROWS_AS((void)compressNv12(frame.image, 90), JpegError);
  frame.image.height = 0;
  CHECK_THROWS_AS((void)compressNv12(frame.image, 90), JpegError);
}

TEST_CASE("YUY2: кадр сжимается в JPEG своих цветов и размера, с выравниванием строк") {
  Yuy2Frame frame;
  fillStripes(frame, 128, 64);
  checkStripes(compressYuy2(frame.image, 90), 128, 64);

  Yuy2Frame padded;
  fillStripes(padded, 129, 65, 12);
  checkStripes(compressYuy2(padded.image, 90), 129, 65);
}

TEST_CASE("YUY2: кадр меньше заявленного размера — ошибка") {
  Yuy2Frame frame;
  fillStripes(frame, 64, 32);
  frame.image.height = 33;
  CHECK_THROWS_AS((void)compressYuy2(frame.image, 90), JpegError);
}
