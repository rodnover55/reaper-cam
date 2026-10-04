#include "cam/capture/backend_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <tuple>

namespace cam::capture {
namespace {

/// Насколько частота может отличаться от целой или NTSC-частоты, чтобы
/// считаться ею, — доля.
constexpr double kRateTolerance = 0.0001;

bool near(double rate, double target) {
  return std::abs(rate - target) <= target * kRateTolerance;
}

} // namespace

CameraMode modeOf(int width, int height, std::int64_t rateNumerator,
                  std::int64_t rateDenominator) {
  CameraMode mode{.width = width, .height = height};
  if (rateNumerator <= 0 || rateDenominator <= 0)
    return mode;

  const double rate =
      static_cast<double>(rateNumerator) / static_cast<double>(rateDenominator);

  const double whole = std::round(rate);
  if (whole >= 1.0 && near(rate, whole)) {
    mode.rateNumerator = static_cast<int>(whole);
    mode.rateDenominator = 1;
    return mode;
  }

  const double ntsc = std::round(rate * 1.001);
  if (ntsc >= 1.0 && near(rate, ntsc * 1000.0 / 1001.0)) {
    mode.rateNumerator = static_cast<int>(ntsc) * 1000;
    mode.rateDenominator = 1001;
    return mode;
  }

  const std::int64_t divisor = std::gcd(rateNumerator, rateDenominator);
  mode.rateNumerator = static_cast<int>(rateNumerator / divisor);
  mode.rateDenominator = static_cast<int>(rateDenominator / divisor);
  return mode;
}

bool sameRate(const CameraMode &mode, std::int64_t rateNumerator,
              std::int64_t rateDenominator) {
  if (rateNumerator <= 0 || rateDenominator <= 0)
    return false;

  const double rate =
      static_cast<double>(rateNumerator) / static_cast<double>(rateDenominator);
  return near(rate, mode.framesPerSecond());
}

namespace {

/// Частоты, которые предлагаются внутри непрерывного диапазона.
constexpr std::array<int, 8> kCommonRates{60, 50, 30, 25, 24, 20, 15, 10};

double rateOf(std::int64_t numerator, std::int64_t denominator) {
  return denominator > 0 ? static_cast<double>(numerator) / static_cast<double>(denominator)
                         : 0.0;
}

} // namespace

bool rateInRange(const CameraMode &mode, std::int64_t lowNumerator,
                 std::int64_t lowDenominator, std::int64_t highNumerator,
                 std::int64_t highDenominator) {
  const double rate = mode.framesPerSecond();
  const double low = rateOf(lowNumerator, lowDenominator);
  const double high = rateOf(highNumerator, highDenominator);
  return rate >= low * (1.0 - kRateTolerance) && rate <= high * (1.0 + kRateTolerance);
}

std::vector<CameraMode> modesInRateRange(int width, int height, std::int64_t lowNumerator,
                                         std::int64_t lowDenominator,
                                         std::int64_t highNumerator,
                                         std::int64_t highDenominator) {
  std::vector<CameraMode> modes{modeOf(width, height, highNumerator, highDenominator)};
  for (const int rate : kCommonRates) {
    const CameraMode common = modeOf(width, height, rate, 1);
    if (rateInRange(common, lowNumerator, lowDenominator, highNumerator, highDenominator))
      modes.push_back(common);
  }
  sortModes(modes);
  return modes;
}

void sortModes(std::vector<CameraMode> &modes) {
  std::ranges::sort(modes, [](const CameraMode &a, const CameraMode &b) {
    return std::tuple(a.width * a.height, a.framesPerSecond()) >
           std::tuple(b.width * b.height, b.framesPerSecond());
  });
  const auto duplicates = std::ranges::unique(modes);
  modes.erase(duplicates.begin(), duplicates.end());
}

std::optional<double> captureTimeFromStamp(double arrival, double stampNow, double stamp,
                                           double longest) {
  const double age = stampNow - stamp;
  if (age < 0.0 || age > longest)
    return std::nullopt;

  return arrival - age;
}

} // namespace cam::capture
