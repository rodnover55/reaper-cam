#include "cam/timing/clock_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cam::timing {
namespace {

/// Отклонение меньше этого выбросом не считается: так ровные пары без
/// дрожания не теряют соседей из-за округления.
constexpr double kSmallestOutlier = 0.005;

double meanOf(const std::vector<double> &values) {
  double sum = 0.0;
  for (const double value : values)
    sum += value;
  return sum / static_cast<double>(values.size());
}

/// Среднее без выбросов: значения дальше четырёх медианных отклонений от
/// среднего (и не ближе 5 мс) в него не входят. Так одиночный скачок не
/// сдвигает прямую, а дрожание пачек PulseAudio остаётся внутри порога.
double robustMeanOf(const std::vector<double> &values) {
  const double mean = meanOf(values);

  std::vector<double> deviations;
  deviations.reserve(values.size());
  for (const double value : values)
    deviations.push_back(std::abs(value - mean));

  std::vector<double> sorted = deviations;
  const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
  std::ranges::nth_element(sorted, middle);
  const double limit = std::max(4.0 * *middle, kSmallestOutlier);

  std::vector<double> inliers;
  inliers.reserve(values.size());
  for (std::size_t i = 0; i < values.size(); ++i)
    if (deviations[i] <= limit)
      inliers.push_back(values[i]);

  return inliers.empty() ? mean : meanOf(inliers);
}

} // namespace

ClockBridge::ClockBridge() : ClockBridge(Settings{}) {}

ClockBridge::ClockBridge(Settings settings) : settings_(settings) {}

void ClockBridge::add(double monotonic, double stream) {
  const Pair pair{.monotonic = monotonic, .stream = stream};

  bool startSegment = segments_.empty();
  if (!startSegment) {
    const double expected = streamIn(segments_.back(), monotonic);
    startSegment = std::abs(stream - expected) > settings_.breakThreshold;
  }

  if (startSegment) {
    segments_.push_back(Segment{.pairs = {}, .first = pair, .last = pair});
    if (segments_.size() > settings_.keptSegments) {
      segments_.erase(segments_.begin());
      firstForgotten_ = true;
    }
  }

  Segment &segment = segments_.back();
  segment.pairs.push_back(pair);
  segment.last = pair;

  while (segment.pairs.front().monotonic < monotonic - settings_.memory)
    segment.pairs.pop_front();
}

double ClockBridge::windowStart(const Segment &segment, double monotonic) const {
  const double latest =
      std::max(segment.first.monotonic, segment.last.monotonic - settings_.window);
  return std::clamp(monotonic - (settings_.window / 2), segment.first.monotonic, latest);
}

double ClockBridge::streamIn(const Segment &segment, double monotonic) const {
  const double from = windowStart(segment, monotonic);
  const double to = from + settings_.window;

  auto begin = std::ranges::lower_bound(segment.pairs, from, {}, &Pair::monotonic);
  auto end = std::ranges::upper_bound(segment.pairs, to, {}, &Pair::monotonic);
  if (begin == end) { // окно ушло из памяти или между парами — ближайшие пары
    begin = end == segment.pairs.begin() ? end : std::prev(end);
    end = std::next(begin);
  }

  // Сдвиги — от первой пары окна: абсолютные моменты монотонных часов
  // велики, и сумма по ним съела бы точность.
  const Pair base = *begin;
  std::vector<double> shifts;
  shifts.reserve(static_cast<std::size_t>(end - begin));
  for (auto p = begin; p != end; ++p)
    shifts.push_back((p->stream - base.stream) - (p->monotonic - base.monotonic));

  return base.stream + robustMeanOf(shifts) + (monotonic - base.monotonic);
}

StreamTime ClockBridge::streamAt(double monotonic) const {
  // До первой пары: звук записи ещё не дошёл или пары ещё придерживались, а
  // прямая та же (findings.md — задержка вывода).
  if (!segments_.empty() && !firstForgotten_ && monotonic < segments_.front().first.monotonic)
    return {.coverage = Coverage::Known, .seconds = streamIn(segments_.front(), monotonic)};

  for (const Segment &segment : segments_) {
    if (monotonic < segment.first.monotonic) // в паузе перед отрезком
      return {.coverage = Coverage::Gap, .seconds = 0.0};

    if (monotonic <= segment.last.monotonic)
      return {.coverage = Coverage::Known, .seconds = streamIn(segment, monotonic)};
  }

  return {.coverage = Coverage::NotYet, .seconds = 0.0};
}

std::optional<double> ClockBridge::extrapolate(double monotonic) const {
  if (segments_.empty() || monotonic < segments_.back().first.monotonic)
    return std::nullopt;

  return streamIn(segments_.back(), monotonic);
}

bool ClockBridge::settled(double monotonic) const {
  if (segments_.empty())
    return false;

  const Segment &last = segments_.back();
  const bool beforeLast = monotonic < last.first.monotonic;
  if (beforeLast && (segments_.size() > 1 || firstForgotten_))
    return true; // прошлый отрезок или пауза перед последним

  // Окно у момента не прижато к концу: пары должны дойти до его края.
  const double from = std::max(last.first.monotonic, monotonic - (settings_.window / 2));
  return last.last.monotonic >= from + settings_.window;
}

std::optional<double> ClockBridge::settledUntil() const {
  if (segments_.empty())
    return std::nullopt;

  const Segment &last = segments_.back();
  if (last.last.monotonic - last.first.monotonic < settings_.window)
    return std::nullopt;

  return last.last.monotonic - (settings_.window / 2);
}

} // namespace cam::timing
