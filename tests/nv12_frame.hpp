#pragma once

// Синтетический кадр NV12 для проверок сжатия: четыре вертикальные полосы —
// чёрная, белая, красная и синяя, в видеодиапазоне BT.601, как их отдала бы
// камера.

#include "cam/capture/jpeg_encoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cam::tests {

struct Rgb {
  int red = 0;
  int green = 0;
  int blue = 0;
};

/// Полоса: её цвет в YCbCr видеодиапазона и каким он выглядит в RGB.
struct Stripe {
  std::uint8_t y;
  std::uint8_t cb;
  std::uint8_t cr;
  Rgb rgb;
};

inline constexpr std::array<Stripe, 4> kStripes{{
    {.y = 16, .cb = 128, .cr = 128, .rgb = {.red = 0, .green = 0, .blue = 0}},
    {.y = 235, .cb = 128, .cr = 128, .rgb = {.red = 255, .green = 255, .blue = 255}},
    {.y = 81, .cb = 90, .cr = 240, .rgb = {.red = 255, .green = 0, .blue = 0}},
    {.y = 41, .cb = 240, .cr = 110, .rgb = {.red = 0, .green = 0, .blue = 255}},
}};

/// Плоскости кадра; `image` смотрит в них, поэтому кадр не копируется.
struct Nv12Frame {
  std::vector<std::uint8_t> luma;
  std::vector<std::uint8_t> chroma;
  capture::Nv12Image image;

  Nv12Frame() = default;
  Nv12Frame(const Nv12Frame &) = delete;
  Nv12Frame &operator=(const Nv12Frame &) = delete;
  Nv12Frame(Nv12Frame &&) = delete;
  Nv12Frame &operator=(Nv12Frame &&) = delete;
  ~Nv12Frame() = default;
};

/// Полоса, в которую попадает столбец `x` кадра шириной `width`.
inline const Stripe &stripeAt(int x, int width) {
  return kStripes[static_cast<std::size_t>(x * 4 / width)];
}

/// Заполняет `frame` полосами. `padding` — лишние байты в конце каждой
/// строки, как у буферов системы с выравниванием.
inline void fillStripes(Nv12Frame &frame, int width, int height, std::size_t padding = 0) {
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  const std::size_t lumaStride = w + padding;
  const std::size_t chromaStride = ((w + 1) / 2 * 2) + padding;
  const std::size_t chromaRows = (h + 1) / 2;

  frame.luma.assign(lumaStride * h, 0);
  frame.chroma.assign(chromaStride * chromaRows, 0);

  for (std::size_t y = 0; y < h; ++y)
    for (std::size_t x = 0; x < w; ++x)
      frame.luma[(y * lumaStride) + x] = stripeAt(static_cast<int>(x), width).y;

  for (std::size_t y = 0; y < chromaRows; ++y)
    for (std::size_t x = 0; x < (w + 1) / 2; ++x) {
      const Stripe &stripe = stripeAt(static_cast<int>(x * 2), width);
      frame.chroma[(y * chromaStride) + (x * 2)] = stripe.cb;
      frame.chroma[(y * chromaStride) + (x * 2) + 1] = stripe.cr;
    }

  frame.image = capture::Nv12Image{.width = width,
                                   .height = height,
                                   .luma = frame.luma,
                                   .lumaStride = lumaStride,
                                   .chroma = frame.chroma,
                                   .chromaStride = chromaStride};
}

/// Тот же кадр в YUY2.
struct Yuy2Frame {
  std::vector<std::uint8_t> data;
  capture::Yuy2Image image;

  Yuy2Frame() = default;
  Yuy2Frame(const Yuy2Frame &) = delete;
  Yuy2Frame &operator=(const Yuy2Frame &) = delete;
  Yuy2Frame(Yuy2Frame &&) = delete;
  Yuy2Frame &operator=(Yuy2Frame &&) = delete;
  ~Yuy2Frame() = default;
};

inline void fillStripes(Yuy2Frame &frame, int width, int height, std::size_t padding = 0) {
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  const std::size_t stride = ((w + 1) / 2 * 4) + padding;
  frame.data.assign(stride * h, 0);

  for (std::size_t y = 0; y < h; ++y)
    for (std::size_t x = 0; x < (w + 1) / 2; ++x) {
      const Stripe &stripe = stripeAt(static_cast<int>(x * 2), width);
      std::uint8_t *pair = &frame.data[(y * stride) + (x * 4)];
      pair[0] = stripe.y;
      pair[1] = stripe.cb;
      pair[2] = stripe.y;
      pair[3] = stripe.cr;
    }

  frame.image = capture::Yuy2Image{
      .width = width, .height = height, .data = frame.data, .stride = stride};
}

} // namespace cam::tests
