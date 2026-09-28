#pragma once

// Сборка заголовков контейнера в памяти: числа big-endian и коробки QuickTime,
// чья длина известна только после содержимого.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cam::container::detail {

class Bytes {
public:
  void u8(std::uint8_t value) { data_.push_back(value); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value >> 8U));
    u8(static_cast<std::uint8_t>(value));
  }

  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value >> 16U));
    u16(static_cast<std::uint16_t>(value));
  }

  void u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value >> 32U));
    u32(static_cast<std::uint32_t>(value));
  }

  /// Четыре символа кода коробки или формата, например "moov".
  void fourcc(std::string_view code) {
    for (std::size_t i = 0; i < 4; ++i)
      u8(i < code.size() ? static_cast<std::uint8_t>(code[i])
                         : static_cast<std::uint8_t>(' '));
  }

  void zeros(std::size_t count) { data_.insert(data_.end(), count, 0); }

  void raw(std::span<const std::uint8_t> bytes) {
    data_.insert(data_.end(), bytes.begin(), bytes.end());
  }

  void text(std::string_view chars) {
    for (const char c : chars)
      u8(static_cast<std::uint8_t>(c));
  }

  void putU32At(std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i)
      data_[at + i] = static_cast<std::uint8_t>(value >> (24U - (8U * i)));
  }

  void putU64At(std::size_t at, std::uint64_t value) {
    putU32At(at, static_cast<std::uint32_t>(value >> 32U));
    putU32At(at + 4, static_cast<std::uint32_t>(value));
  }

  std::size_t size() const { return data_.size(); }
  const std::vector<std::uint8_t> &data() const { return data_; }
  void clear() { data_.clear(); }

private:
  std::vector<std::uint8_t> data_;
};

inline std::array<std::uint8_t, 8> bigEndian64(std::uint64_t value) {
  std::array<std::uint8_t, 8> out{};
  for (std::size_t i = 0; i < 8; ++i)
    out[i] = static_cast<std::uint8_t>(value >> (56U - (8U * i)));
  return out;
}

/// Коробка QuickTime: длина и код, длина проставляется при закрытии.
class Box {
public:
  Box(Bytes &bytes, std::string_view type) : bytes_(bytes), start_(bytes.size()) {
    bytes_.u32(0);
    bytes_.fourcc(type);
  }

  ~Box() { bytes_.putU32At(start_, static_cast<std::uint32_t>(bytes_.size() - start_)); }

  Box(const Box &) = delete;
  Box &operator=(const Box &) = delete;
  Box(Box &&) = delete;
  Box &operator=(Box &&) = delete;

private:
  Bytes &bytes_;
  std::size_t start_;
};

} // namespace cam::container::detail
