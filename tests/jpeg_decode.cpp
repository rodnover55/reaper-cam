#include "jpeg_decode.hpp"

#include <WDL/lice/lice.h>

#include <cstddef>
#include <memory>

namespace cam::tests {

std::optional<DecodedImage> decodeJpeg(std::span<const std::uint8_t> jpeg) {
  const std::unique_ptr<LICE_IBitmap> bitmap(
      LICE_LoadJPGFromMemory(jpeg.data(), static_cast<int>(jpeg.size())));
  if (!bitmap)
    return std::nullopt;

  DecodedImage image;
  image.width = bitmap->getWidth();
  image.height = bitmap->getHeight();
  image.rgb.reserve(static_cast<std::size_t>(image.width) *
                    static_cast<std::size_t>(image.height) * 3);

  const LICE_pixel *bits = bitmap->getBits();
  const int span = bitmap->getRowSpan();

  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const LICE_pixel pixel = bits[(static_cast<std::ptrdiff_t>(y) * span) + x];
      image.rgb.push_back(static_cast<std::uint8_t>(LICE_GETR(pixel)));
      image.rgb.push_back(static_cast<std::uint8_t>(LICE_GETG(pixel)));
      image.rgb.push_back(static_cast<std::uint8_t>(LICE_GETB(pixel)));
    }
  }

  return image;
}

} // namespace cam::tests
