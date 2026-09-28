#include <doctest/doctest.h>

#include "cam/capture/test_pattern.hpp"
#include "cam/container/jpeg_tables.hpp"
#include "jpeg_decode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using cam::capture::TestPattern;
using cam::container::hasHuffmanTables;
using cam::container::withHuffmanTables;
using cam::tests::decodeJpeg;

namespace {

/// Кадр как у камеры UVC: сегменты DHT вырезаны.
std::vector<std::uint8_t> withoutHuffmanTables(const std::vector<std::uint8_t> &jpeg) {
  std::vector<std::uint8_t> out{jpeg[0], jpeg[1]};
  std::size_t at = 2;

  while (at + 3 < jpeg.size() && jpeg[at + 1] != 0xDA) {
    const std::size_t length = (std::size_t{jpeg[at + 2]} << 8U) | jpeg[at + 3];
    if (jpeg[at + 1] != 0xC4)
      out.insert(out.end(), jpeg.begin() + static_cast<std::ptrdiff_t>(at),
                 jpeg.begin() + static_cast<std::ptrdiff_t>(at + 2 + length));
    at += 2 + length;
  }

  out.insert(out.end(), jpeg.begin() + static_cast<std::ptrdiff_t>(at), jpeg.end());
  return out;
}

/// Сжатые данные изображения: всё от маркера SOS до конца.
std::vector<std::uint8_t> scanOf(const std::vector<std::uint8_t> &jpeg) {
  const std::vector<std::uint8_t> sos{0xFF, 0xDA};
  const auto found = std::ranges::search(jpeg, sos);
  return {found.begin(), jpeg.end()};
}

} // namespace

TEST_CASE("кадр без таблиц Хаффмана после дополнения разжимается, данные изображения те же") {
  const TestPattern pattern(640, 360);
  const std::vector<std::uint8_t> original = pattern.render(1.5, 42.0);
  const std::vector<std::uint8_t> stripped = withoutHuffmanTables(original);

  REQUIRE(hasHuffmanTables(original));
  REQUIRE_FALSE(hasHuffmanTables(stripped));
  CHECK_FALSE(decodeJpeg(stripped)); // jpeglib без таблиц не читает

  const std::vector<std::uint8_t> completed = withHuffmanTables(stripped);
  CHECK(hasHuffmanTables(completed));
  CHECK(scanOf(completed) == scanOf(original));

  const auto decoded = decodeJpeg(completed);
  REQUIRE(decoded);
  CHECK(decoded->width == 640);
  CHECK(decoded->height == 360);

  // Таблицы те же стандартные, что у кодера, — картинка та же до точки.
  const auto reference = decodeJpeg(original);
  REQUIRE(reference);
  CHECK(decoded->rgb == reference->rgb);
}

TEST_CASE("кадр с таблицами и не JPEG остаются как есть") {
  const TestPattern pattern(160, 90);
  const std::vector<std::uint8_t> original = pattern.render(0.0, 0.0);
  CHECK(withHuffmanTables(original) == original);

  const std::vector<std::uint8_t> garbage{1, 2, 3, 4, 5};
  CHECK(withHuffmanTables(garbage) == garbage);
  CHECK_FALSE(hasHuffmanTables(garbage));
}
