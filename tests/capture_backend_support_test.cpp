#include <doctest/doctest.h>

#include "cam/capture/backend_support.hpp"

#include <vector>

using cam::capture::CameraMode;
using cam::capture::captureTimeFromStamp;
using cam::capture::modeOf;
using cam::capture::modesInRateRange;
using cam::capture::rateInRange;
using cam::capture::sameRate;
using cam::capture::sortModes;

TEST_CASE("режим: длительность кадра, округлённая системой, даёт целую частоту") {
  // Media Foundation: 333333 сотни наносекунд на кадр.
  CHECK(modeOf(1280, 720, 10000000, 333333) ==
        CameraMode{.width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1});
  // AVFoundation: 33333 микросекунды.
  CHECK(modeOf(640, 480, 1000000, 33333) ==
        CameraMode{.width = 640, .height = 480, .rateNumerator = 30, .rateDenominator = 1});
  CHECK(modeOf(640, 480, 15, 1) ==
        CameraMode{.width = 640, .height = 480, .rateNumerator = 15, .rateDenominator = 1});
}

TEST_CASE("режим: частота NTSC остаётся дробью 1001") {
  CHECK(modeOf(1920, 1080, 30000, 1001) ==
        CameraMode{
            .width = 1920, .height = 1080, .rateNumerator = 30000, .rateDenominator = 1001});
  // 29,97 кадра в секунду, записанные длительностью в сотнях наносекунд.
  CHECK(modeOf(1920, 1080, 10000000, 333667) ==
        CameraMode{
            .width = 1920, .height = 1080, .rateNumerator = 30000, .rateDenominator = 1001});
}

TEST_CASE("режим: необычная частота сокращается, но не выравнивается") {
  CHECK(modeOf(320, 240, 75, 10) ==
        CameraMode{.width = 320, .height = 240, .rateNumerator = 15, .rateDenominator = 2});
}

TEST_CASE("режим: частота сравнивается с той же точностью, с какой выравнивается") {
  const CameraMode mode{
      .width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1};
  CHECK(sameRate(mode, 10000000, 333333));
  CHECK(sameRate(mode, 30, 1));
  CHECK_FALSE(sameRate(mode, 30000, 1001));
  CHECK_FALSE(sameRate(mode, 25, 1));
  CHECK_FALSE(sameRate(mode, 0, 1));
}

TEST_CASE("режимы: крупные и частые первыми, без повторов") {
  std::vector<CameraMode> modes{
      {.width = 640, .height = 480, .rateNumerator = 30, .rateDenominator = 1},
      {.width = 1280, .height = 720, .rateNumerator = 15, .rateDenominator = 1},
      {.width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1},
      {.width = 640, .height = 480, .rateNumerator = 30, .rateDenominator = 1},
  };
  sortModes(modes);

  CHECK(modes == std::vector<CameraMode>{
                     {.width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1},
                     {.width = 1280, .height = 720, .rateNumerator = 15, .rateDenominator = 1},
                     {.width = 640, .height = 480, .rateNumerator = 30, .rateDenominator = 1},
                 });
}

TEST_CASE("режимы диапазона: верхний край и обычные частоты внутри, по убыванию") {
  // Встроенная камера Mac: от 1 до 30 кадров; длительности — как их сообщает
  // AVFoundation.
  const auto modes = modesInRateRange(1920, 1080, 1, 1, 1000000, 33333);
  std::vector<int> rates;
  for (const CameraMode &mode : modes) {
    CHECK(mode.width == 1920);
    CHECK(mode.rateDenominator == 1);
    rates.push_back(mode.rateNumerator);
  }
  CHECK(rates == std::vector<int>{30, 25, 24, 20, 15, 10});
}

TEST_CASE("режимы диапазона: диапазон из одной частоты — один режим") {
  const auto modes = modesInRateRange(640, 480, 30000, 1001, 30000, 1001);
  REQUIRE(modes.size() == 1);
  CHECK(modes[0].rateNumerator == 30000);
  CHECK(modes[0].rateDenominator == 1001);
}

TEST_CASE("режимы диапазона: частота внутри, на краях и снаружи") {
  const CameraMode at30{
      .width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1};
  CHECK(rateInRange(at30, 1, 1, 1000000, 33333));
  CHECK(rateInRange(at30, 30, 1, 30, 1));
  CHECK_FALSE(rateInRange(at30, 1, 1, 25, 1));
  CHECK_FALSE(rateInRange(at30, 60, 1, 60, 1));
}

TEST_CASE("время съёмки: метка в чужих часах переносится по возрасту кадра") {
  // Кадр пришёл в 100,000 по часам расширения; часы метки тогда показывали
  // 5000,040, а метка кадра — 5000,000: кадр снят на 40 мс раньше прихода.
  const auto shot = captureTimeFromStamp(100.0, 5000.040, 5000.0);
  REQUIRE(shot.has_value());
  CHECK(*shot == doctest::Approx(99.960));
}

TEST_CASE("время съёмки: сбитая метка не принимается") {
  // Метка позже прихода.
  CHECK_FALSE(captureTimeFromStamp(100.0, 5000.0, 5000.010).has_value());
  // Метка старше полусекунды.
  CHECK_FALSE(captureTimeFromStamp(100.0, 5000.0, 4999.0).has_value());
  // Граница — своя для вызывающего.
  CHECK(captureTimeFromStamp(100.0, 5000.0, 4999.0, 2.0).has_value());
}
