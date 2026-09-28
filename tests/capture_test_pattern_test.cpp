#include <doctest/doctest.h>

#include "cam/capture/jpeg_encoder.hpp"
#include "cam/capture/test_pattern.hpp"
#include "jpeg_decode.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using cam::capture::compressBlackFrame;
using cam::capture::compressJpeg;
using cam::capture::JpegError;
using cam::capture::readTestPattern;
using cam::capture::TestPattern;
using cam::tests::decodeJpeg;

TEST_CASE("кадр тестового источника разжимается через WDL в картинку нужного размера") {
  const TestPattern pattern(1280, 720);
  const std::vector<std::uint8_t> jpeg = pattern.render(12.345, 83.456);

  REQUIRE(jpeg.size() > 4);
  CHECK(jpeg[0] == 0xFF);
  CHECK(jpeg[1] == 0xD8); // SOI
  CHECK(jpeg[jpeg.size() - 2] == 0xFF);
  CHECK(jpeg[jpeg.size() - 1] == 0xD9); // EOI

  const auto image = decodeJpeg(jpeg);
  REQUIRE(image);
  CHECK(image->width == 1280);
  CHECK(image->height == 720);
}

TEST_CASE("числа тестового источника читаются с разжатого кадра") {
  struct Size {
    int width;
    int height;
  };

  for (const Size size :
       {Size{.width = 1280, .height = 720}, Size{.width = 640, .height = 480},
        Size{.width = 320, .height = 180}}) {
    CAPTURE(size.width);
    CAPTURE(size.height);

    const TestPattern pattern(size.width, size.height);
    const auto image = decodeJpeg(pattern.render(3600.042, -1.5));
    REQUIRE(image);

    const auto reading = readTestPattern(image->rgb, image->width, image->height);
    REQUIRE(reading);
    CHECK(reading->fileTimeMs == 3600042);
    CHECK(reading->timelinePositionMs == -1500);
  }
}

TEST_CASE("на чужом кадре чисел тестового источника нет") {
  const std::vector<std::uint8_t> black(std::size_t{64} * 48 * 3, 0);
  CHECK_FALSE(readTestPattern(black, 64, 48));
  CHECK_FALSE(readTestPattern(black, 64, 47));
}

TEST_CASE("сжатие отказывает на размере, не совпадающем с буфером") {
  const std::vector<std::uint8_t> rgb(std::size_t{16} * 16 * 3, 0);
  CHECK_THROWS_AS(compressJpeg(rgb, 16, 15, 90), JpegError);
  CHECK_THROWS_AS(compressJpeg(rgb, 0, 16, 90), JpegError);
}

TEST_CASE("чёрный кадр разжимается в чёрную картинку размера режима") {
  for (const auto &[width, height] : {std::pair{1280, 720}, std::pair{640, 480}}) {
    CAPTURE(width);
    CAPTURE(height);

    const auto image = decodeJpeg(compressBlackFrame(width, height));
    REQUIRE(image);
    CHECK(image->width == width);
    CHECK(image->height == height);
    CHECK(std::ranges::all_of(image->rgb, [](std::uint8_t value) { return value < 4; }));
  }
}
