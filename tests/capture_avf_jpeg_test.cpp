#include <doctest/doctest.h>

#include "cam/capture_avf/jpeg_compressor.hpp"
#include "jpeg_decode.hpp"
#include "nv12_frame.hpp"

#include <cstddef>
#include <cstdlib>

using cam::capture_avf::compressNv12WithVideoToolbox;
using cam::tests::decodeJpeg;
using cam::tests::fillStripes;
using cam::tests::kStripes;
using cam::tests::Nv12Frame;

TEST_CASE("VideoToolbox: кадр NV12 встроенной камеры сжимается в JPEG, который читает WDL") {
  Nv12Frame frame;
  fillStripes(frame, 1280, 720);

  const auto jpeg = compressNv12WithVideoToolbox(frame.image, cam::capture_avf::kJpegQuality);
  REQUIRE(jpeg);

  const auto image = decodeJpeg(*jpeg);
  REQUIRE(image);
  CHECK(image->width == 1280);
  CHECK(image->height == 720);

  const std::size_t y = 360;
  for (std::size_t stripe = 0; stripe < kStripes.size(); ++stripe) {
    CAPTURE(stripe);
    const std::size_t x = (1280 * ((2 * stripe) + 1)) / 8;
    const std::size_t at = ((y * 1280) + x) * 3;
    CHECK(std::abs(image->rgb[at] - kStripes[stripe].rgb.red) < 24);
    CHECK(std::abs(image->rgb[at + 1] - kStripes[stripe].rgb.green) < 24);
    CHECK(std::abs(image->rgb[at + 2] - kStripes[stripe].rgb.blue) < 24);
  }
}
