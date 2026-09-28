#include <doctest/doctest.h>

#include "format_config.hpp"

#include <cstdint>
#include <string>
#include <vector>

using cam::capture::CameraMode;
using cam::reaper::decodeConfig;
using cam::reaper::encodeConfig;
using cam::reaper::FormatConfig;
using cam::reaper::isFormatConfig;
using cam::reaper::recordConfigOf;

namespace {

FormatConfig laptopCamera() {
  return FormatConfig{
      .cameraId = "by-id/usb-CN09357G8LG009BLAFRPA01_Integrated_Webcam_HD-video-index0",
      .cameraName = "Integrated_Webcam_HD",
      .mode =
          CameraMode{.width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1}};
}

} // namespace

TEST_CASE("конфигурация формата: запись и чтение дают то же") {
  const FormatConfig config = laptopCamera();
  const std::vector<std::uint8_t> bytes = encodeConfig(config);

  REQUIRE(isFormatConfig(bytes));
  CHECK(bytes[0] == 'm'); // REAPER узнаёт формат по первым четырём байтам
  CHECK(bytes[3] == 'r');

  const auto decoded = decodeConfig(bytes);
  REQUIRE(decoded);
  CHECK(*decoded == config);

  FormatConfig ntsc = config;
  ntsc.mode.rateNumerator = 30000;
  ntsc.mode.rateDenominator = 1001;
  CHECK(decodeConfig(encodeConfig(ntsc)) == ntsc);
}

TEST_CASE("конфигурация формата: один код — камера не выбрана") {
  const std::vector<std::uint8_t> code{'m', 'a', 'c', 'r'};
  const auto decoded = decodeConfig(code);
  REQUIRE(decoded);
  CHECK(decoded->cameraId.empty());
}

TEST_CASE("конфигурация формата: неизвестная версия, обрыв и мусор не роняют разбор") {
  std::vector<std::uint8_t> bytes = encodeConfig(laptopCamera());

  SUBCASE("версия из будущего") {
    bytes[4] = 2;
    CHECK_FALSE(decodeConfig(bytes));
  }

  SUBCASE("оборвана на каждом байте") {
    for (std::size_t length = 5; length < bytes.size(); ++length) {
      CAPTURE(length);
      const std::vector<std::uint8_t> cut(bytes.begin(),
                                          bytes.begin() + static_cast<std::ptrdiff_t>(length));
      CHECK_FALSE(decodeConfig(cut));
    }
  }

  SUBCASE("чужой код") {
    bytes[0] = 'e';
    CHECK_FALSE(isFormatConfig(bytes));
    CHECK_FALSE(decodeConfig(bytes));
  }

  SUBCASE("нулевой размер кадра") {
    FormatConfig broken = laptopCamera();
    broken.mode.width = 0;
    CHECK_FALSE(decodeConfig(encodeConfig(broken)));
  }
}

TEST_CASE("конфигурация формата: из состояния дорожки") {
  // Так REAPER отдаёт дорожку с форматом расширения и тестовым источником.
  const std::string chunk =
      "<TRACK\n"
      "  NAME cam\n"
      "  REC 1 0 1 0 5 0 0 0\n"
      "  <RECCFG 1\n"
      "    bWFjcgEAAACAAgAAaAEAAB4AAAABAAAADAB0ZXN0L3BhdHRlcm4UAFRlc3QgcGF0dGVybiAo\n"
      "    ZGVidWcp\n"
      "  >\n"
      "  <FXCHAIN\n"
      "  >\n"
      ">\n";
  const auto bytes = recordConfigOf(chunk);
  REQUIRE(bytes);
  const auto config = decodeConfig(*bytes);
  REQUIRE(config);
  CHECK(config->cameraId == "test/pattern");
  CHECK(config->cameraName == "Test pattern (debug)");
  CHECK(config->mode.width == 640);
  CHECK(config->mode.height == 360);

  // Только код формата: «macr» в base64.
  const auto code = recordConfigOf("<TRACK\n  <RECCFG 1\n    bWFjcg==\n  >\n>\n");
  REQUIRE(code);
  CHECK(*code == std::vector<std::uint8_t>{'m', 'a', 'c', 'r'});

  CHECK_FALSE(recordConfigOf("<TRACK\n  NAME audio\n>\n"));
  CHECK_FALSE(recordConfigOf("<TRACK\n  <RECCFG 1\n    !!!\n  >\n>\n"));
}
