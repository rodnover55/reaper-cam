#include <doctest/doctest.h>

#include "cam/timing/clock_bridge.hpp"

#include <cmath>
#include <random>

using cam::timing::ClockBridge;
using cam::timing::Coverage;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
constexpr double kBlockSeconds = kBlock / kSampleRate;

/// Время потока в начале блока номер `block`.
double at(long block) { return static_cast<double>(block) * kBlockSeconds; }

/// Звуковая карта, чьи часы уходят от монотонных на `ppm` миллионных долей, и
/// звуковой поток, который зовёт аудиохук с дрожанием до длины блока.
///
/// Дрожание — с нулевым средним: постоянное запаздывание вызовов сдвигает
/// всё на одну и ту же долю блока, не растёт и меньше кадра; проверяется
/// здесь другое — что шум усредняется, а расхождение часов отслеживается.
struct SoundCard {
  double ppm;
  double start = 1000.0; // монотонные часы давно идут
  std::mt19937 random{42};
  std::uniform_real_distribution<double> jitter{-kBlockSeconds / 2, kBlockSeconds / 2};

  double rate() const { return 1.0 + (ppm * 1e-6); }

  /// Когда на самом деле обрабатывался момент потока.
  double trueMonotonic(double stream) const { return start + (stream / rate()); }

  double trueStream(double monotonic) const { return (monotonic - start) * rate(); }

  /// Момент вызова аудиохука для блока номер `block`.
  double callback(long block) { return trueMonotonic(at(block)) + jitter(random); }
};

} // namespace

TEST_CASE("мост часов: ошибка меньше 2 мс с первой секунды и не растёт за час") {
  for (const double ppm : {+100.0, -100.0}) {
    CAPTURE(ppm);
    SoundCard card{.ppm = ppm};
    ClockBridge bridge;

    const long blocksPerHour = std::lround(3600.0 / kBlockSeconds);
    double worst = 0.0;

    for (long block = 0; block < blocksPerHour; ++block) {
      bridge.add(card.callback(block), at(block));

      // Раз в секунду переводим момент, который писатель переводил бы сейчас:
      // последний, чей перевод устоялся, — за полокна до последнего блока.
      if (block % 86 != 0)
        continue;

      const double stream = (at(block)) - 0.6;
      if (stream < 1.0)
        continue;

      const double monotonic = card.trueMonotonic(stream);
      REQUIRE(bridge.settled(monotonic));
      const auto converted = bridge.streamAt(monotonic);
      REQUIRE(converted.coverage == Coverage::Known);

      worst = std::max(worst, std::abs(converted.seconds - stream));
    }

    // Окно — секунда, 86 пар: дрожание до полублока усредняется до долей
    // миллисекунды, худшее за час — около 1.3 мс.
    MESSAGE("худшая ошибка за час, мс: " << worst * 1000.0);
    CHECK(worst < 0.002);
  }
}

TEST_CASE("мост часов: до начала записи и после последнего блока") {
  const SoundCard card{.ppm = 0.0};
  ClockBridge bridge;

  for (long block = 0; block < 500; ++block)
    bridge.add(card.trueMonotonic(at(block)), at(block));

  // До начала записи — по продолжению прямой назад: время файла отрицательное.
  const auto before = bridge.streamAt(card.start - 0.5);
  REQUIRE(before.coverage == Coverage::Known);
  CHECK(before.seconds == doctest::Approx(-0.5).epsilon(1e-9));

  CHECK(bridge.streamAt(card.trueMonotonic(at(500)) + 1.0).coverage == Coverage::NotYet);
  const auto after = bridge.extrapolate(card.trueMonotonic(at(500)) + 1.0);
  REQUIRE(after);
  CHECK(*after == doctest::Approx(at(500) + 1.0).epsilon(1e-9));

  const auto inside = bridge.streamAt(card.trueMonotonic(3.0));
  REQUIRE(inside.coverage == Coverage::Known);
  CHECK(inside.seconds == doctest::Approx(3.0).epsilon(1e-9));
}

TEST_CASE("мост часов: пауза — разрыв, после неё новый отрезок") {
  const SoundCard card{.ppm = 50.0};
  ClockBridge bridge;

  // 5 секунд записи, пауза 3 секунды, ещё 5 секунд. Во время паузы сэмплов
  // нет: время потока стоит, монотонное идёт.
  const long blocks = std::lround(5.0 / kBlockSeconds);
  for (long block = 0; block < blocks; ++block)
    bridge.add(card.trueMonotonic(at(block)), at(block));

  const double pause = 3.0;
  for (long block = blocks; block < 2 * blocks; ++block)
    bridge.add(card.trueMonotonic(at(block)) + pause, at(block));

  CHECK(bridge.segmentCount() == 2);

  const double pausedAt = card.trueMonotonic(at(blocks));
  CHECK(bridge.streamAt(pausedAt + 1.5).coverage == Coverage::Gap);

  const auto before = bridge.streamAt(card.trueMonotonic(4.0));
  REQUIRE(before.coverage == Coverage::Known);
  CHECK(std::abs(before.seconds - 4.0) < 0.0005);

  // После паузы время потока продолжается с того же места: 7 секунд потока
  // обрабатывались на 3 секунды позже, чем без паузы.
  const auto after = bridge.streamAt(card.trueMonotonic(7.0) + pause);
  REQUIRE(after.coverage == Coverage::Known);
  CHECK(std::abs(after.seconds - 7.0) < 0.0005);

  // Отрезок до паузы кончился: его переводы больше не меняются.
  CHECK(bridge.settled(card.trueMonotonic(4.9)));
}

TEST_CASE("мост часов: одиночный скачок пары не сдвигает прямую") {
  const SoundCard card{.ppm = 0.0};
  ClockBridge bridge;

  for (long block = 0; block < 1000; ++block) {
    const double stream = at(block);
    // Одна пара на 0.22 с в стороне — как слышимая позиция в первом блоке
    // записи (findings.md — задержка вывода).
    const double outlier = block == 500 ? 0.22 : 0.0;
    bridge.add(card.trueMonotonic(stream), stream + outlier);
  }

  const auto converted = bridge.streamAt(card.trueMonotonic(at(505)));
  REQUIRE(converted.coverage == Coverage::Known);
  CHECK(std::abs(converted.seconds - at(505)) < 0.0005);
}

TEST_CASE("мост часов: перевод устоялся, когда пары после момента заполнили полокна") {
  const SoundCard card{.ppm = 0.0};
  ClockBridge bridge; // окно — секунда

  const double moment = card.trueMonotonic(2.0);
  long block = 0;
  for (; at(block) < 2.4; ++block)
    bridge.add(card.trueMonotonic(at(block)), at(block));
  CHECK_FALSE(bridge.settled(moment));

  for (; at(block) < 2.6; ++block)
    bridge.add(card.trueMonotonic(at(block)), at(block));
  CHECK(bridge.settled(moment));

  // Начало записи: окно прижато к первой паре и должно заполниться целиком.
  ClockBridge start;
  for (block = 0; at(block) < 0.9; ++block)
    start.add(card.trueMonotonic(at(block)), at(block));
  CHECK_FALSE(start.settled(card.trueMonotonic(0.1)));
  for (; at(block) < 1.1; ++block)
    start.add(card.trueMonotonic(at(block)), at(block));
  CHECK(start.settled(card.trueMonotonic(0.1)));
}
