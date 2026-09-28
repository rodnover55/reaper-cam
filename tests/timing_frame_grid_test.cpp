#include <doctest/doctest.h>

#include "cam/timing/clock_bridge.hpp"
#include "cam/timing/frame_grid.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

using cam::timing::ClockBridge;
using cam::timing::Coverage;
using cam::timing::FrameGrid;
using cam::timing::Rate;
using cam::timing::Slot;

namespace {

FrameGrid::Settings at30() {
  return {
      .rate = Rate{.numerator = 30, .denominator = 1}, .decisionLag = 0.2, .gapLimit = 1.0};
}

std::vector<Slot> drain(FrameGrid &grid, std::vector<Slot> into = {}) {
  for (const Slot &slot : grid.take())
    into.push_back(slot);
  return into;
}

std::vector<Slot> finish(FrameGrid &grid, std::vector<Slot> slots) {
  for (const Slot &slot : grid.finish())
    slots.push_back(slot);
  return slots;
}

/// Время съёмки кадра номер `index` камеры с частотой `fps`: чуть позже
/// ровного момента, чтобы не решать равенства.
double shot(std::uint64_t index, double fps) {
  return (static_cast<double>(index) / fps) + 0.004;
}

} // namespace

TEST_CASE("сетка: 60 секунд при 30 кадрах — 1800 мест, в каждом свой кадр") {
  FrameGrid grid(at30());
  std::vector<Slot> slots;

  for (std::uint64_t frame = 0; frame <= 1800; ++frame) {
    grid.addFrame(frame, shot(frame, 30.0));
    grid.advance(std::min(60.0, shot(frame, 30.0)));
    slots = drain(grid, slots);
  }

  grid.advance(60.0);
  slots = finish(grid, slots);

  REQUIRE(slots.size() == 1800);
  for (std::uint64_t k = 0; k < slots.size(); ++k) {
    CAPTURE(k);
    CHECK(slots[k].index == k);
    REQUIRE(slots[k].frame);
    CHECK(*slots[k].frame == k);
  }
}

TEST_CASE("сетка: камера на 15 кадрах даёт каждый кадр дважды") {
  FrameGrid grid(at30());
  std::vector<Slot> slots;

  for (std::uint64_t frame = 0; frame < 150; ++frame) {
    grid.addFrame(frame, shot(frame, 15.0));
    grid.advance(shot(frame, 15.0));
    slots = drain(grid, slots);
  }

  grid.advance(10.0);
  slots = finish(grid, slots);
  REQUIRE(slots.size() == 300);

  std::map<std::uint64_t, int> uses;
  for (const Slot &slot : slots) {
    REQUIRE(slot.frame);
    ++uses[*slot.frame];
  }

  CHECK(uses.size() == 150);
  for (const auto &[frame, count] : uses) {
    CAPTURE(frame);
    CHECK(count == 2);
  }
}

TEST_CASE("сетка: пропуск повторяет прошлый кадр, пропуск дольше секунды — чёрный кадр") {
  FrameGrid grid(at30());
  std::vector<Slot> slots;

  // Кадры до 5 секунд, потом три секунды ничего, потом снова.
  std::uint64_t id = 0;
  for (std::uint64_t frame = 0; frame < 300; ++frame) {
    const double time = shot(frame, 30.0);
    if (time > 5.0 && time < 8.0)
      continue;
    grid.addFrame(id++, time);
  }

  grid.advance(10.0);
  slots = finish(grid, slots);
  REQUIRE(slots.size() == 300);

  const auto slotAt = [&](double seconds) {
    return slots[static_cast<std::size_t>(std::lround(seconds * 30.0))];
  };
  const std::uint64_t lastBeforeGap = 149; // кадр в 4.970 с

  REQUIRE(slotAt(5.5).frame);
  CHECK(*slotAt(5.5).frame == lastBeforeGap);
  CHECK_FALSE(slotAt(6.5).frame);
  REQUIRE(slotAt(8.5).frame);
  CHECK(*slotAt(8.5).frame > lastBeforeGap);
}

TEST_CASE("сетка: кадр чуть раньше начала файла достаётся месту 0, если он ближе всех") {
  FrameGrid grid(at30());
  grid.addFrame(0, -0.2);
  grid.addFrame(1, -0.005);
  grid.addFrame(2, 0.030);
  grid.addFrame(3, 0.063);
  grid.advance(0.05);
  const std::vector<Slot> slots = finish(grid, {});

  REQUIRE(slots.size() == 2);
  CHECK(slots[0].frame == 1U);
  CHECK(slots[1].frame == 2U);
}

TEST_CASE("сетка: кадры до начала записи и в паузе не попадают в файл") {
  // Мост с паузой: 5 секунд записи, 3 секунды паузы, ещё 5 секунд. Камера
  // снимает всё время, в том числе за секунду до записи и в паузе.
  constexpr double kBlock = 512.0 / 44100.0;
  constexpr double kStart = 100.0;
  constexpr double kPause = 3.0;
  ClockBridge bridge;

  const long blocks = std::lround(5.0 / kBlock);
  for (long block = 0; block < 2 * blocks; ++block) {
    const double stream = static_cast<double>(block) * kBlock;
    const double monotonic = kStart + stream + (block >= blocks ? kPause : 0.0);
    bridge.add(monotonic, stream);
  }

  FrameGrid grid(at30());
  std::set<std::uint64_t> notRecorded;
  std::uint64_t id = 0;

  for (int shotIndex = 0; shotIndex < 14 * 30; ++shotIndex) {
    const double monotonic = kStart - 1.0 + (shotIndex / 30.0);
    const auto time = bridge.streamAt(monotonic + 0.004);
    const std::uint64_t frame = id++;

    if (time.coverage != Coverage::Known || time.seconds < 0.0) {
      notRecorded.insert(frame);
      continue;
    }

    grid.addFrame(frame, time.seconds);
  }

  const double streamEnd = static_cast<double>(2 * blocks) * kBlock;
  grid.advance(streamEnd);
  const std::vector<Slot> slots = finish(grid, {});

  CHECK(notRecorded.size() > 100); // секунда до записи и три секунды паузы
  CHECK(slots.size() == static_cast<std::size_t>(std::ceil(streamEnd * 30.0)));

  for (const Slot &slot : slots) {
    CAPTURE(slot.index);
    REQUIRE(slot.frame);
    CHECK(notRecorded.count(*slot.frame) == 0);
  }
}
