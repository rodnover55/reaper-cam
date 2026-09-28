#include "format_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

namespace cam::reaper {
namespace {

/// Длина строк ограничена двумя байтами длины; признаки и имена камер
/// короче на порядки.
constexpr std::size_t kLongestText = std::numeric_limits<std::uint16_t>::max();

void put32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putText(std::vector<std::uint8_t> &out, const std::string &text) {
  const std::size_t length = std::min(text.size(), kLongestText);
  out.push_back(static_cast<std::uint8_t>(length));
  out.push_back(static_cast<std::uint8_t>(length >> 8U));
  out.insert(out.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
}

/// Чтение по порядку с проверкой границ: оборванная конфигурация не читается
/// за концом.
class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  std::optional<std::uint32_t> u32() {
    if (at_ + 4 > bytes_.size())
      return std::nullopt;

    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
      value |= static_cast<std::uint32_t>(bytes_[at_ + i]) << (8U * i);
    at_ += 4;
    return value;
  }

  std::optional<std::string> text() {
    if (at_ + 2 > bytes_.size())
      return std::nullopt;

    const std::size_t length = bytes_[at_] | (static_cast<std::size_t>(bytes_[at_ + 1]) << 8U);
    at_ += 2;
    if (at_ + length > bytes_.size())
      return std::nullopt;

    std::string value(bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
                      bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + length));
    at_ += length;
    return value;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t at_ = 0;
};

/// Значение символа base64; пусто — символ не из алфавита.
std::optional<std::uint32_t> base64Value(char symbol) {
  if (symbol >= 'A' && symbol <= 'Z')
    return static_cast<std::uint32_t>(symbol - 'A');
  if (symbol >= 'a' && symbol <= 'z')
    return static_cast<std::uint32_t>(symbol - 'a' + 26);
  if (symbol >= '0' && symbol <= '9')
    return static_cast<std::uint32_t>(symbol - '0' + 52);
  if (symbol == '+')
    return 62U;
  if (symbol == '/')
    return 63U;
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> decodeBase64(std::string_view text) {
  std::vector<std::uint8_t> out;
  std::uint32_t bits = 0;
  unsigned count = 0;
  for (const char symbol : text) {
    if (symbol == '=')
      break;
    const auto value = base64Value(symbol);
    if (!value)
      return std::nullopt;

    bits = (bits << 6U) | *value;
    count += 6;
    if (count >= 8) {
      count -= 8;
      out.push_back(static_cast<std::uint8_t>(bits >> count));
    }
  }
  return out;
}

std::string_view trimmed(std::string_view line) {
  const auto first = line.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const auto last = line.find_last_not_of(" \t\r");
  return line.substr(first, last - first + 1);
}

} // namespace

bool isFormatConfig(std::span<const std::uint8_t> bytes) {
  return bytes.size() >= kFormatCodeBytes.size() &&
         std::equal(kFormatCodeBytes.begin(), kFormatCodeBytes.end(), bytes.begin());
}

std::vector<std::uint8_t> encodeConfig(const FormatConfig &config) {
  std::vector<std::uint8_t> out(kFormatCodeBytes.begin(), kFormatCodeBytes.end());
  put32(out, FormatConfig::kVersion);
  put32(out, static_cast<std::uint32_t>(config.mode.width));
  put32(out, static_cast<std::uint32_t>(config.mode.height));
  put32(out, static_cast<std::uint32_t>(config.mode.rateNumerator));
  put32(out, static_cast<std::uint32_t>(config.mode.rateDenominator));
  putText(out, config.cameraId);
  putText(out, config.cameraName);
  return out;
}

std::optional<FormatConfig> decodeConfig(std::span<const std::uint8_t> bytes) {
  if (!isFormatConfig(bytes))
    return std::nullopt;

  // Один код — формат выбран, камера ещё нет.
  if (bytes.size() == kFormatCodeBytes.size())
    return FormatConfig{};

  Reader reader(bytes.subspan(kFormatCodeBytes.size()));
  const auto version = reader.u32();
  if (version != FormatConfig::kVersion)
    return std::nullopt;

  const auto width = reader.u32();
  const auto height = reader.u32();
  const auto numerator = reader.u32();
  const auto denominator = reader.u32();
  auto cameraId = reader.text();
  auto cameraName = reader.text();
  if (!width || !height || !numerator || !denominator || !cameraId || !cameraName)
    return std::nullopt;

  // Размер и частота — положительные и в разумных пределах: мусор не должен
  // дойти до драйвера камеры.
  constexpr std::uint32_t kLargest = 1U << 16U;
  if (*width == 0 || *height == 0 || *numerator == 0 || *denominator == 0 ||
      *width > kLargest || *height > kLargest || *numerator > kLargest * 16 ||
      *denominator > kLargest * 16)
    return std::nullopt;

  return FormatConfig{
      .cameraId = std::move(*cameraId),
      .cameraName = std::move(*cameraName),
      .mode = capture::CameraMode{.width = static_cast<int>(*width),
                                  .height = static_cast<int>(*height),
                                  .rateNumerator = static_cast<int>(*numerator),
                                  .rateDenominator = static_cast<int>(*denominator)}};
}

std::optional<std::vector<std::uint8_t>> recordConfigOf(std::string_view trackChunk) {
  // Блок идёт строками: «<RECCFG 1», строки base64, «>».
  std::string encoded;
  bool inside = false;
  std::size_t at = 0;
  while (at < trackChunk.size()) {
    const std::size_t end = std::min(trackChunk.find('\n', at), trackChunk.size());
    const std::string_view line = trimmed(trackChunk.substr(at, end - at));
    at = end + 1;

    if (!inside) {
      inside = line.starts_with("<RECCFG");
      continue;
    }
    if (line == ">")
      return decodeBase64(encoded);
    encoded += line;
  }
  return std::nullopt;
}

} // namespace cam::reaper
