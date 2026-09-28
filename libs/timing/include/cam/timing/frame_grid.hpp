#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace cam::timing {

/// Частота кадров дробью: 30/1, 30000/1001.
struct Rate {
  int numerator = 30;
  int denominator = 1;

  /// Момент места номер `index` от начала файла, в секундах.
  double timeOf(std::uint64_t index) const {
    return static_cast<double>(index) * denominator / numerator;
  }
};

/// Что стоит в месте сетки.
struct Slot {
  std::uint64_t index = 0;
  /// Кадр места; пусто — чёрный кадр.
  std::optional<std::uint64_t> frame;
};

/// Ровная сетка кадров по времени файла (design.md D4).
///
/// Место номер k — момент k / частота от начала файла. Каждое место получает
/// кадр, снятый ближе всего к этому моменту; нет кадра — повторяется
/// предыдущий, лишние кадры отбрасываются. Если ближайший кадр дальше
/// секунды — камера пропала, и место получает чёрный кадр.
///
/// Место решается, когда пришёл первый кадр после его момента: ближе уже ничего
/// не придёт. Если кадра нет, место ждёт, пока поток не уйдёт вперёд на
/// `decisionLag`, и решается тем, что есть.
class FrameGrid {
public:
  struct Settings {
    Rate rate;
    /// Сколько поток ждёт кадра после момента места, прежде чем повторить
    /// предыдущий. Это задержка записи файла, а не синхронности.
    double decisionLag = 0.2;
    /// Ближайший кадр дальше этого — чёрный кадр.
    double gapLimit = 1.0;
  };

  explicit FrameGrid(Settings settings);

  /// Кадр `id`, снятый во время файла `fileTime`. Кадры — в порядке съёмки.
  /// Кадр до начала файла достаётся только месту 0 — если он к нему ближе
  /// всех.
  void addFrame(std::uint64_t id, double fileTime);

  /// Сэмплы записаны до времени файла `fileTime`: мест в файле столько,
  /// сколько моментов раньше него.
  void advance(double fileTime);

  /// Места, решённые с прошлого вызова, по порядку.
  std::vector<Slot> take();

  /// Запись кончилась: решает все оставшиеся места тем, что есть.
  std::vector<Slot> finish();

  /// Сколько мест решено.
  std::uint64_t decided() const { return next_; }

private:
  struct Frame {
    std::uint64_t id;
    double fileTime;
  };

  /// Ближайшие к моменту кадры: последний до него и первый после.
  struct Neighbours {
    const Frame *before = nullptr;
    const Frame *after = nullptr;
  };

  std::vector<Slot> decide(bool final);
  Neighbours neighboursOf(double moment);
  std::optional<std::uint64_t> choose(const Neighbours &around, double moment) const;

  Settings settings_;
  std::deque<Frame> frames_;
  double streamEnd_ = 0.0;
  std::uint64_t next_ = 0;
};

} // namespace cam::timing
