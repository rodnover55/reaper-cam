#include "cam/capture/test_pattern.hpp"

#include "cam/capture/jpeg_encoder.hpp"

#include <WDL/lice/lice.h>

#include <cmath>
#include <cstddef>
#include <fmt/format.h>
#include <string>

namespace cam::capture {
namespace {

// Кадр рисуется на холсте 160 × 90 и растягивается до размера кадра без
// сглаживания. Встроенный шрифт LICE — 8 точек, и на холсте такого размера
// строка читается и в 1280 × 720, и в 320 × 180.
constexpr int kCanvasWidth = 160;
constexpr int kCanvasHeight = 90;

// Полосы клеток: по одной на число. Клетка 4 × 8 точек холста; первая и
// последняя клетки полосы — белые ограничители, между ними 32 бита числа,
// старший первым. Белая клетка — единица.
constexpr int kCellWidth = 4;
constexpr int kCellHeight = 8;
constexpr int kBarLeft = 4;
constexpr int kBits = 32;
constexpr int kCells = kBits + 2;
constexpr int kFileTimeBarTop = 62;
constexpr int kPositionBarTop = 76;

constexpr LICE_pixel kWhite = LICE_RGBA(255U, 255U, 255U, 255U);
constexpr LICE_pixel kBlack = LICE_RGBA(0U, 0U, 0U, 255U);
constexpr LICE_pixel kBackground = LICE_RGBA(24U, 24U, 24U, 255U);

std::uint32_t toMilliseconds(double seconds) {
  // Отрицательная позиция хранится в дополнительном коде: при чтении знак
  // восстанавливается.
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(seconds * 1000.0)));
}

void drawBar(LICE_IBitmap *canvas, int top, std::uint32_t value) {
  for (int cell = 0; cell < kCells; ++cell) {
    bool white = true; // ограничители
    if (cell > 0 && cell <= kBits)
      white = ((value >> (kBits - cell)) & 1U) != 0;

    LICE_FillRect(canvas, kBarLeft + (cell * kCellWidth), top, kCellWidth, kCellHeight,
                  white ? kWhite : kBlack, 1.0F, LICE_BLIT_MODE_COPY);
  }
}

std::vector<std::uint8_t> toRgb(LICE_IBitmap &bitmap) {
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  const int span = bitmap.getRowSpan();
  const LICE_pixel *bits = bitmap.getBits();

  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 3);
  std::size_t out = 0;

  for (int y = 0; y < height; ++y) {
    const LICE_pixel *row = bits + (static_cast<std::ptrdiff_t>(y) * span);
    for (int x = 0; x < width; ++x) {
      const LICE_pixel pixel = row[x];
      rgb[out++] = static_cast<std::uint8_t>(LICE_GETR(pixel));
      rgb[out++] = static_cast<std::uint8_t>(LICE_GETG(pixel));
      rgb[out++] = static_cast<std::uint8_t>(LICE_GETB(pixel));
    }
  }

  return rgb;
}

/// Яркость точки кадра, заданной в координатах холста (центр клетки).
int brightnessAt(std::span<const std::uint8_t> rgb, int width, int height, double canvasX,
                 double canvasY) {
  const auto x = static_cast<int>(canvasX * width / kCanvasWidth);
  const auto y = static_cast<int>(canvasY * height / kCanvasHeight);
  const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                          static_cast<std::size_t>(x)) *
                         3;
  return (rgb[at] + rgb[at + 1] + rgb[at + 2]) / 3;
}

std::optional<std::uint32_t> readBar(std::span<const std::uint8_t> rgb, int width, int height,
                                     int top) {
  std::uint32_t value = 0;

  for (int cell = 0; cell < kCells; ++cell) {
    const double x = kBarLeft + (cell * kCellWidth) + (kCellWidth / 2.0);
    const double y = top + (kCellHeight / 2.0);
    const bool white = brightnessAt(rgb, width, height, x, y) > 128;

    if (cell == 0 || cell == kCells - 1) {
      if (!white)
        return std::nullopt;
      continue;
    }

    value = (value << 1U) | (white ? 1U : 0U);
  }

  return value;
}

} // namespace

TestPattern::TestPattern(int width, int height, int quality)
    : width_(width), height_(height), quality_(quality) {}

std::vector<std::uint8_t> TestPattern::render(double fileTime, double timelinePosition) const {
  LICE_MemBitmap canvas(kCanvasWidth, kCanvasHeight);
  LICE_Clear(&canvas, kBackground);

  const std::string fileLine = fmt::format("file {:9.3f}", fileTime);
  const std::string positionLine = fmt::format("pos  {:9.3f}", timelinePosition);
  LICE_DrawText(&canvas, 4, 4, fileLine.c_str(), kWhite, 1.0F, LICE_BLIT_MODE_COPY);
  LICE_DrawText(&canvas, 4, 16, positionLine.c_str(), kWhite, 1.0F, LICE_BLIT_MODE_COPY);

  drawBar(&canvas, kFileTimeBarTop, toMilliseconds(fileTime));
  drawBar(&canvas, kPositionBarTop, toMilliseconds(timelinePosition));

  LICE_MemBitmap frame(width_, height_);
  LICE_ScaledBlit(&frame, &canvas, 0, 0, width_, height_, 0.0F, 0.0F,
                  static_cast<float>(kCanvasWidth), static_cast<float>(kCanvasHeight), 1.0F,
                  LICE_BLIT_MODE_COPY | LICE_BLIT_FILTER_NONE);

  return compressJpeg(toRgb(frame), width_, height_, quality_);
}

std::optional<TestPatternReading> readTestPattern(std::span<const std::uint8_t> rgb, int width,
                                                  int height) {
  if (width <= 0 || height <= 0 ||
      rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3)
    return std::nullopt;

  const auto fileTime = readBar(rgb, width, height, kFileTimeBarTop);
  const auto position = readBar(rgb, width, height, kPositionBarTop);
  if (!fileTime || !position)
    return std::nullopt;

  return TestPatternReading{.fileTimeMs = static_cast<std::int32_t>(*fileTime),
                            .timelinePositionMs = static_cast<std::int32_t>(*position)};
}

} // namespace cam::capture
