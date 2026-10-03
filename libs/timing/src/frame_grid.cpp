#include "cam/timing/frame_grid.hpp"

#include <algorithm>
#include <cmath>

namespace cam::timing {

FrameGrid::FrameGrid(Settings settings) : settings_(settings) {}

void FrameGrid::addFrame(std::uint64_t id, double fileTime) {
  // Кадры идут в порядке съёмки; если время всё же пошло назад (мост
  // пересчитал прямую), старый хвост ближе к будущим местам не станет.
  while (!frames_.empty() && frames_.back().fileTime > fileTime)
    frames_.pop_back();

  frames_.push_back({.id = id, .fileTime = fileTime});
}

void FrameGrid::advance(double fileTime) { streamEnd_ = std::max(streamEnd_, fileTime); }

std::vector<Slot> FrameGrid::take() { return decide(false); }

std::vector<Slot> FrameGrid::finish() { return decide(true); }

std::vector<Slot> FrameGrid::decide(bool final) {
  std::vector<Slot> slots;

  for (;;) {
    const double moment = settings_.rate.timeOf(next_);
    if (moment >= streamEnd_)
      break;

    const Neighbours around = neighboursOf(moment);
    if (!around.after && !final && streamEnd_ < moment + settings_.decisionLag)
      break; // кадр после момента ещё может прийти

    slots.push_back(Slot{.index = next_, .frame = choose(around, moment)});
    ++next_;
  }

  return slots;
}

FrameGrid::Neighbours FrameGrid::neighboursOf(double moment) {
  // Кадры до момента, кроме последнего, уже никому не нужны: следующие места
  // позже, и последний кадр до них всегда ближе.
  while (frames_.size() >= 2 && frames_[1].fileTime <= moment)
    frames_.pop_front();

  Neighbours around;
  if (frames_.empty())
    return around;

  if (frames_.front().fileTime <= moment) {
    around.before = &frames_.front();
    if (frames_.size() >= 2)
      around.after = &frames_[1];
  } else {
    around.after = &frames_.front();
  }

  return around;
}

std::optional<std::uint64_t> FrameGrid::choose(const Neighbours &around, double moment) const {
  const Frame *chosen = around.before;
  if (around.after &&
      (!around.before || around.after->fileTime - moment < moment - around.before->fileTime))
    chosen = around.after;

  if (!chosen || std::abs(chosen->fileTime - moment) > settings_.gapLimit)
    return std::nullopt; // камеры нет дольше секунды — чёрный кадр

  return chosen->id;
}

} // namespace cam::timing
