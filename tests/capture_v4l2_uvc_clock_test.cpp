#include <doctest/doctest.h>

#include "cam/capture_v4l2/uvc_clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <vector>

using cam::capture_v4l2::parseUvcMetadata;
using cam::capture_v4l2::UvcClock;
using cam::capture_v4l2::UvcSample;

namespace {

void put16(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value));
  put16(out, static_cast<std::uint16_t>(value >> 16U));
}

void put64(std::vector<std::uint8_t> &out, std::uint64_t value) {
  put32(out, static_cast<std::uint32_t>(value));
  put32(out, static_cast<std::uint32_t>(value >> 32U));
}

/// Запись узла метаданных: приписка драйвера и заголовок пакета UVC.
void entry(std::vector<std::uint8_t> &out, std::uint64_t ns, std::uint16_t sof,
           std::optional<std::uint32_t> pts, std::optional<std::uint32_t> stc,
           std::uint16_t deviceSof) {
  const auto length = static_cast<std::uint8_t>(2 + (pts ? 4 : 0) + (stc ? 6 : 0));
  const auto flags =
      static_cast<std::uint8_t>(0x80U | (pts ? 0x04U : 0U) | (stc ? 0x08U : 0U));
  put64(out, ns);
  put16(out, sof);
  out.push_back(length);
  out.push_back(flags);
  if (pts)
    put32(out, *pts);
  if (stc) {
    put32(out, *stc);
    put16(out, deviceSof);
  }
}

/// Кадр симуляции: PTS и настоящее начало его съёмки.
struct Exposure {
  std::uint32_t pts;
  double seconds;
};

/// Камера и хост, как у камеры ноутбука: часы камеры 15 МГц со своим ходом,
/// пакеты каждые 0.27 мс, хост обрабатывает их пачками по 16 — задержка от 0
/// до 4 мс, плюс дрожание. Номера кадров шины у камеры — со своим сдвигом.
struct Simulation {
  double clockHz = 15e6 * (1.0 + 40e-6);
  double hostStart = 473000.0; // секунды монотонных часов
  std::uint32_t stcStart = 4'000'000'000U;
  int sofOffset = 0;
  std::mt19937 random{7};

  std::uint32_t stcAt(double seconds) const {
    const double ticks = static_cast<double>(stcStart) + ((seconds - hostStart) * clockHz);
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::llround(ticks)) &
                                      0xFFFFFFFFU);
  }

  /// Пакеты за `seconds` секунд от `from`. Первый пакет каждого кадра несёт PTS
  /// начала его съёмки — за 2 мс до передачи; кадры идут в `exposures` по
  /// порядку.
  std::vector<UvcSample> packets(double from, double seconds, double framePeriod,
                                 std::vector<Exposure> &exposures) {
    std::uniform_real_distribution<double> jitter(0.0, 0.0003);
    std::vector<UvcSample> out;
    double batchEnd = 0.0;
    int inBatch = 0;
    double nextFrame = from;

    const auto count = static_cast<int>(seconds / 0.00027);
    for (int index = 0; index < count; ++index) {
      const double t = from + (index * 0.00027);
      if (inBatch == 0)
        batchEnd = t + (16 * 0.00027);
      inBatch = (inBatch + 1) % 16;

      UvcSample sample;
      sample.hostNs =
          static_cast<std::uint64_t>(std::llround((batchEnd + jitter(random)) * 1e9));
      const auto busFrame = static_cast<int>(std::floor(t * 1000.0));
      sample.hostSof = static_cast<std::uint16_t>((busFrame + 3) & 0x7FF);
      sample.deviceSof = static_cast<std::uint16_t>((busFrame + sofOffset) & 0x7FF);
      sample.stc = stcAt(t);

      if (t >= nextFrame) {
        const double exposure = nextFrame - 0.002;
        sample.pts = stcAt(exposure);
        exposures.push_back({.pts = *sample.pts, .seconds = exposure});
        nextFrame += framePeriod;
      }

      out.push_back(sample);
    }

    return out;
  }
};

/// Худшая ошибка времени съёмки по кадрам, начавшимся после `from`.
double worstError(UvcClock &clock, const std::vector<UvcSample> &samples,
                  const std::vector<Exposure> &exposures, double from) {
  double worst = 0.0;
  std::size_t frame = 0;

  for (const UvcSample &sample : samples) {
    clock.add(sample);
    if (!sample.pts)
      continue;

    const double truth = exposures.at(frame++).seconds;
    if (truth < from)
      continue; // разгон

    const auto start = clock.exposureStart(*sample.pts);
    REQUIRE(start);
    worst = std::max(worst, std::abs(*start - truth));
  }

  return worst;
}

} // namespace

TEST_CASE("метаданные UVC: записи разбираются с PTS, SCR и без них") {
  std::vector<std::uint8_t> buffer;
  entry(buffer, 1000, 5, 111U, 222U, 7);
  entry(buffer, 2000, 6, std::nullopt, 333U, 8);
  entry(buffer, 3000, 2047, std::nullopt, std::nullopt, 0);

  const auto samples = parseUvcMetadata(buffer);
  REQUIRE(samples.size() == 3);

  CHECK(samples[0].hostNs == 1000);
  CHECK(samples[0].hostSof == 5);
  CHECK(samples[0].pts == 111U);
  CHECK(samples[0].stc == 222U);
  CHECK(samples[0].deviceSof == 7);

  CHECK_FALSE(samples[1].pts);
  CHECK(samples[1].stc == 333U);

  CHECK_FALSE(samples[2].pts);
  CHECK_FALSE(samples[2].stc);
  CHECK(samples[2].hostSof == 2047);

  // Оборванная запись в конце буфера не читается.
  buffer.pop_back();
  CHECK(parseUvcMetadata(buffer).size() == 2);
}

TEST_CASE("часы UVC: начало съёмки восстанавливается точнее миллисекунды при любом сдвиге "
          "номеров кадров шины") {
  for (const int sofOffset : {0, 36, -580, 1024}) {
    CAPTURE(sofOffset);

    Simulation camera;
    camera.sofOffset = sofOffset;
    UvcClock clock;
    std::vector<Exposure> exposures;

    const auto samples = camera.packets(camera.hostStart, 8.0, 1.0 / 30.0, exposures);
    CHECK(worstError(clock, samples, exposures, camera.hostStart + 2.0) < 0.001);

    REQUIRE(clock.deviceClockHz());
    CHECK(*clock.deviceClockHz() == doctest::Approx(15e6 * (1.0 + 40e-6)).epsilon(1e-5));
  }
}

TEST_CASE("часы UVC: переполнение 32-битных часов камеры") {
  Simulation camera;
  camera.stcStart = 0xFFFFFFFFU - 15'000'000U; // переполнение через секунду
  UvcClock clock;
  std::vector<Exposure> exposures;

  const auto samples = camera.packets(camera.hostStart, 4.0, 1.0 / 30.0, exposures);
  CHECK(worstError(clock, samples, exposures, camera.hostStart + 0.5) < 0.001);
}

TEST_CASE("часы UVC: без отсчётов и при мусоре связи нет") {
  UvcClock clock;
  CHECK_FALSE(clock.exposureStart(12345U));

  std::mt19937 random{1};
  std::uniform_int_distribution<std::uint32_t> any;
  std::uint64_t ns = 1'000'000'000'000ULL;
  for (int i = 0; i < 5000; ++i) {
    UvcSample sample;
    ns += 270000;
    sample.hostNs = ns;
    sample.stc = any(random);
    clock.add(sample);
  }

  CHECK_FALSE(clock.exposureStart(12345U));
}

TEST_CASE("часы UVC: буферы узла метаданных — от разбора до начала съёмки") {
  // Как отдаёт драйвер: буфер на кадр видео, в нём заголовки всех пакетов
  // кадра подряд; первый пакет кадра несёт PTS начала его съёмки. Номера
  // кадров шины у камеры — со своим сдвигом, как у настоящих камер.
  Simulation camera;
  camera.sofOffset = 1468;
  std::vector<Exposure> exposures;
  const auto packets = camera.packets(camera.hostStart, 3.0, 1.0 / 30.0, exposures);

  std::vector<std::vector<std::uint8_t>> buffers;
  for (const UvcSample &packet : packets) {
    if (packet.pts || buffers.empty())
      buffers.emplace_back();
    entry(buffers.back(), packet.hostNs, packet.hostSof, packet.pts, packet.stc,
          packet.deviceSof);
  }

  UvcClock clock;
  std::size_t frame = 0;
  std::size_t timed = 0;
  double worst = 0.0;
  double worstSettled = 0.0;

  for (const auto &buffer : buffers) {
    const auto samples = parseUvcMetadata(buffer);
    REQUIRE_FALSE(samples.empty());

    std::optional<std::uint32_t> pts;
    for (const UvcSample &sample : samples) {
      clock.add(sample);
      if (!pts && sample.pts)
        pts = sample.pts;
    }
    REQUIRE(pts);

    const double truth = exposures.at(frame++).seconds;
    const auto start = clock.exposureStart(*pts);
    if (!start)
      continue; // связь ещё не набрала отсчётов

    ++timed;
    CAPTURE(frame);
    // Съёмка — раньше, чем хост обработал первый пакет кадра.
    CHECK(*start < static_cast<double>(samples.front().hostNs) / 1e9);
    const double error = std::abs(*start - truth);
    worst = std::max(worst, error);
    if (truth >= camera.hostStart + 0.5)
      worstSettled = std::max(worstSettled, error);
  }

  MESSAGE("кадров со временем от камеры: " << timed << " из " << frame
                                           << "; худшая ошибка, мс: " << worst * 1000.0
                                           << ", после разгона: " << worstSettled * 1000.0);
  CHECK(timed + 5 >= frame);
  // Первые доли секунды связь держится на немногих отсчётах; дальше ошибка —
  // доли миллисекунды.
  CHECK(worst < 0.002);
  CHECK(worstSettled < 0.001);
}
