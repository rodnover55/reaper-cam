#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cam::tests {

/// Разжатая картинка: строки подряд, по три байта на точку.
struct DecodedImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb;
};

/// Разжимает JPEG через LICE из WDL — тем же кодом, каким WDL читает JPEG у
/// хоста. Пусто, если WDL кадр не прочёл.
std::optional<DecodedImage> decodeJpeg(std::span<const std::uint8_t> jpeg);

} // namespace cam::tests
