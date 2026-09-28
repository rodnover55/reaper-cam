#include "cam/capture_v4l2/uvc_clock.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cam::capture_v4l2 {
namespace {

// Флаги заголовка пакета UVC (bmHeaderInfo, спецификация UVC 1.5, 2.4.3.3).
constexpr std::uint8_t kHasPts = 0x04;
constexpr std::uint8_t kHasScr = 0x08;

// struct uvc_meta_buf: ns (8), sof (2), length (1), flags (1), остаток заголовка.
constexpr std::size_t kEntryHead = 12;
constexpr std::size_t kDriverPart = 10; // ns и sof — то, что приписал драйвер

/// Связь пересчитывается не чаще: кадры идут десятками в секунду, а прямая за
/// четверть секунды не меняется.
constexpr double kRefitNs = 250e6;

std::uint16_t le16(const std::uint8_t *at) {
  return static_cast<std::uint16_t>(at[0] | (at[1] << 8U));
}

std::uint32_t le32(const std::uint8_t *at) {
  return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8U) |
         (static_cast<std::uint32_t>(at[2]) << 16U) |
         (static_cast<std::uint32_t>(at[3]) << 24U);
}

std::uint64_t le64(const std::uint8_t *at) {
  return static_cast<std::uint64_t>(le32(at)) |
         (static_cast<std::uint64_t>(le32(at + 4)) << 32U);
}

struct Slope {
  double slope;
  double meanX;
  double meanY;
};

/// Прямая по наименьшим квадратам; координаты — от первой точки, чтобы
/// большие абсолютные значения не съели точность.
std::optional<Slope> leastSquares(const std::vector<std::pair<double, double>> &points) {
  if (points.size() < 2)
    return std::nullopt;

  double meanX = 0.0;
  double meanY = 0.0;
  for (const auto &[x, y] : points) {
    meanX += x;
    meanY += y;
  }
  meanX /= static_cast<double>(points.size());
  meanY /= static_cast<double>(points.size());

  double sxx = 0.0;
  double sxy = 0.0;
  for (const auto &[x, y] : points) {
    sxx += (x - meanX) * (x - meanX);
    sxy += (x - meanX) * (y - meanY);
  }

  if (sxx <= 0.0)
    return std::nullopt;

  return Slope{.slope = sxy / sxx, .meanX = meanX, .meanY = meanY};
}

double percentile(std::vector<double> values, double share) {
  const auto at = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
  std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(at));
  return values[at];
}

} // namespace

std::vector<UvcSample> parseUvcMetadata(std::span<const std::uint8_t> buffer) {
  std::vector<UvcSample> samples;
  std::size_t at = 0;

  while (at + kEntryHead <= buffer.size()) {
    const std::uint8_t *entry = buffer.data() + at;
    const std::size_t length = entry[10]; // заголовок пакета вместе с байтами длины и флагов
    if (length < 2 || at + kDriverPart + length > buffer.size())
      break;

    const std::uint8_t flags = entry[11];
    const std::uint8_t *field = entry + kEntryHead;
    std::size_t left = length - 2;

    UvcSample sample;
    sample.hostNs = le64(entry);
    sample.hostSof = static_cast<std::uint16_t>(le16(entry + 8) & 0x7FFU);

    if ((flags & kHasPts) != 0 && left >= 4) {
      sample.pts = le32(field);
      field += 4;
      left -= 4;
    }

    if ((flags & kHasScr) != 0 && left >= 6) {
      sample.stc = le32(field);
      sample.deviceSof = static_cast<std::uint16_t>(le16(field + 4) & 0x7FFU);
    }

    samples.push_back(sample);
    at += kDriverPart + length;
  }

  return samples;
}

UvcClock::UvcClock() : UvcClock(Settings{}) {}

UvcClock::UvcClock(Settings settings) : settings_(settings) {}

void UvcClock::add(const UvcSample &sample) {
  if (!sample.stc)
    return;

  // Часы камеры — 32 бита: при 15 МГц они переполняются раз в пять минут.
  if (lastStc_)
    ticks_ += static_cast<double>(static_cast<std::int32_t>(*sample.stc - *lastStc_));
  else
    ticks_ = static_cast<double>(*sample.stc);
  lastStc_ = sample.stc;

  const auto hostNs = static_cast<double>(sample.hostNs);
  points_.push_back({.ticks = ticks_, .hostNs = hostNs});
  while (!points_.empty() && points_.front().hostNs < hostNs - (settings_.window * 1e9))
    points_.pop_front();

  if (cached_ && hostNs - cachedAtNs_ > kRefitNs)
    cached_.reset();
}

std::optional<UvcClock::Line> UvcClock::fit() const {
  if (cached_)
    return cached_;

  if (points_.size() < settings_.minimumSamples)
    return std::nullopt;

  const Point origin = points_.front();
  std::vector<std::pair<double, double>> all;
  all.reserve(points_.size());
  for (const Point &p : points_)
    all.emplace_back(p.ticks - origin.ticks, p.hostNs - origin.hostNs);

  // Первая прямая — через всё облако: её наклон почти верен, а смещение
  // завышено средней задержкой обработки.
  const auto rough = leastSquares(all);
  if (!rough)
    return std::nullopt;

  const auto residualsOf = [&all](const Slope &line) {
    std::vector<double> residuals;
    residuals.reserve(all.size());
    for (const auto &[x, y] : all)
      residuals.push_back(y - (line.meanY + (line.slope * (x - line.meanX))));
    return residuals;
  };

  // Вторая — по нижней доле точек: пакеты, обработанные почти сразу.
  const std::vector<double> roughResiduals = residualsOf(*rough);
  const double cut = percentile(roughResiduals, settings_.lowerShare);
  std::vector<std::pair<double, double>> lower;
  for (std::size_t i = 0; i < all.size(); ++i)
    if (roughResiduals[i] <= cut)
      lower.push_back(all[i]);

  const auto envelope = leastSquares(lower);
  if (!envelope || envelope->slope <= 0.0)
    return std::nullopt;

  // Дно облака под второй прямой: на него она и опускается.
  const std::vector<double> residuals = residualsOf(*envelope);
  const double bottom = percentile(residuals, settings_.bottomPercentile);
  const double spread = percentile(residuals, 1.0 - settings_.bottomPercentile) - bottom;

  // Часы камеры — от сотен килогерц до гигагерца, а задержка обработки —
  // доли пакета USB. Иначе связь неустойчива, и лучше метка буфера.
  const double hz = 1e9 / envelope->slope;
  if (hz < 1e5 || hz > 1e9 || spread > settings_.widestSpreadSeconds * 1e9)
    return std::nullopt;

  cached_ = Line{.originTicks = origin.ticks + envelope->meanX,
                 .originNs = origin.hostNs + envelope->meanY + bottom,
                 .nsPerTick = envelope->slope};
  cachedAtNs_ = points_.back().hostNs;
  return cached_;
}

std::optional<double> UvcClock::exposureStart(std::uint32_t pts) const {
  const auto line = fit();
  if (!line || !lastStc_)
    return std::nullopt;

  // PTS — недавний отсчёт тех же часов: переполнение снимается от последнего SCR.
  const double ticks = ticks_ + static_cast<std::int32_t>(pts - *lastStc_);
  return (line->originNs + ((ticks - line->originTicks) * line->nsPerTick)) / 1e9;
}

std::optional<double> UvcClock::deviceClockHz() const {
  const auto line = fit();
  if (!line)
    return std::nullopt;
  return 1e9 / line->nsPerTick;
}

} // namespace cam::capture_v4l2
