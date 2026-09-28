#include <doctest/doctest.h>

#include "take.hpp"

#include "cam/capture/jpeg_encoder.hpp"
#include "cam/capture/test_pattern.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using cam::capture::CameraMode;
using cam::capture::Frame;
using cam::capture::TestPattern;
using cam::reaper::CameraFeed;
using cam::reaper::HookHistory;
using cam::reaper::Take;

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBlock = 512;
constexpr double kBlockSeconds = static_cast<double>(kBlock) / kSampleRate;
constexpr double kStart = 1000.0;  // монотонное время первого блока записи
constexpr double kPosition = 10.0; // позиция начала записи
constexpr double kOutput = 0.020;  // задержка вывода

/// Вывод в общий буфер: файл остаётся доступен тесту после дубля.
class SharedOutput : public cam::container::Output {
public:
  explicit SharedOutput(std::shared_ptr<std::vector<std::uint8_t>> data)
      : data_(std::move(data)) {}

  void append(std::span<const std::uint8_t> bytes) override {
    data_->insert(data_->end(), bytes.begin(), bytes.end());
  }
  void patch(std::uint64_t offset, std::span<const std::uint8_t> bytes) override {
    std::ranges::copy(bytes, data_->begin() + static_cast<std::ptrdiff_t>(offset));
  }
  std::uint64_t size() const override { return data_->size(); }
  void flush() override {}

private:
  std::shared_ptr<std::vector<std::uint8_t>> data_;
};

/// Камера: кадры по расписанию, потеря — по команде.
struct FakeFeed : CameraFeed {
  std::deque<Frame> *frames;
  std::optional<std::string> *loss;

  FakeFeed(std::deque<Frame> *queue, std::optional<std::string> *lost)
      : frames(queue), loss(lost) {}

  std::optional<Frame> pop() override {
    if (frames->empty())
      return std::nullopt;
    Frame frame = std::move(frames->front());
    frames->pop_front();
    return frame;
  }

  std::optional<std::string> lost() const override { return *loss; }
};

/// Запись, как её видит дубль: хук, блоки экземпляра, кадры камеры.
struct Recording {
  HookHistory history;
  std::deque<Frame> camera;
  std::optional<std::string> loss;
  std::vector<std::string> reports;
  std::shared_ptr<std::vector<std::uint8_t>> file =
      std::make_shared<std::vector<std::uint8_t>>();
  std::vector<std::vector<std::uint8_t>> shots; // кадры камеры по номеру
  TestPattern pattern{160, 90};

  std::unique_ptr<Take> start(bool withCamera = true) {
    Take::Settings settings;
    settings.sampleRate = kSampleRate;
    settings.mode =
        CameraMode{.width = 160, .height = 90, .rateNumerator = 30, .rateDenominator = 1};
    settings.cameraName = "Integrated_Webcam_HD";
    settings.clock = [this] { return clockNow; };

    Take::Acquire acquire;
    if (withCamera)
      acquire = [this] { return std::make_unique<FakeFeed>(&camera, &loss); };

    return std::make_unique<Take>(
        settings, std::make_unique<SharedOutput>(file), history, acquire,
        [this](const std::string &text) { reports.push_back(text); }, false);
  }

  /// Кадр камеры, снятый в момент `captureTime`; номер — по порядку съёмки.
  void shoot(double captureTime) {
    shots.push_back(pattern.render(static_cast<double>(shots.size()) / 30.0, 0.0));
    Frame frame;
    frame.jpeg = shots.back();
    frame.captureTime = captureTime;
    frame.sequence = shots.size() - 1;
    camera.push_back(std::move(frame));
  }

  /// Слышимая позиция при обработке блока с позиции `position`.
  double heardAt(double position) const {
    // После Record хост держит слышимую позицию на начале, пока звук записи
    // не дошёл до выхода (findings.md — задержка вывода).
    if (holdAtStart)
      return std::max(kPosition, position - output);
    return position - output;
  }

  /// Блоки записи: хук, затем экземпляр; кадры камеры — по мере съёмки.
  void record(Take &take, long firstBlock, long blocks, double pause = 0.0) {
    for (long block = firstBlock; block < firstBlock + blocks; ++block) {
      const double stream = static_cast<double>(block) * kBlockSeconds;
      const double monotonic = kStart + stream + pause;
      clockNow = monotonic;
      history.append({.monotonic = monotonic,
                      .position = kPosition + stream,
                      .heard = heardAt(kPosition + stream),
                      .state = 5,
                      .length = kBlock});
      take.onBlock(
          {.firstSample = block * kBlock, .length = kBlock, .position = kPosition + stream});
      shootUntil(monotonic);

      if (block % 4 == 0)
        take.step(false);
    }
  }

  void shootUntil(double monotonic) {
    while (!shotsPlanned.empty() && shotsPlanned.front() <= monotonic) {
      shoot(shotsPlanned.front());
      shotsPlanned.pop_front();
    }
  }

  /// После остановки обработанное ещё звучит задержку вывода — камера снимает
  /// кадры хвоста, пока дубль их ждёт.
  void tail() { shootUntil(clockNow + output + 0.03); }

  double clockNow = 0.0; // имитация монотонных часов: момент последнего блока
  double output = kOutput;
  bool holdAtStart = false;
  std::deque<double> shotsPlanned;

  /// Кадры по 30 в секунду: кадр номер i снят, когда звучало время файла
  /// i / 30 + 4 мс от начала записи, начатой на `from` секунд позже kStart.
  void planShots(double seconds, double from = 0.0) {
    for (int i = 0; i < static_cast<int>(seconds * 30.0); ++i)
      shotsPlanned.push_back(kStart + from + (i / 30.0) + 0.004 + output);
  }

  /// Сколько раз байты кадра встречаются в файле.
  std::size_t count(const std::vector<std::uint8_t> &frame) const {
    std::size_t found = 0;
    for (auto from = file->begin();; ++from) {
      from = std::search(from, file->end(), frame.begin(), frame.end());
      if (from == file->end())
        return found;
      ++found;
    }
  }

  /// Каждое место получило свой кадр: место k — кадр номер k.
  void checkEverySlot(std::int64_t samples) const {
    const auto expectedSlots =
        static_cast<std::size_t>(std::ceil(static_cast<double>(samples) / kSampleRate * 30.0));
    const auto order = slotsInFile();
    REQUIRE(order.size() == expectedSlots);
    for (std::size_t slot = 0; slot < order.size(); ++slot) {
      CAPTURE(slot);
      REQUIRE(order[slot]);
      CHECK(*order[slot] == slot);
    }
  }

  /// Номера кадров камеры в файле по порядку мест: по байтам кадров.
  std::vector<std::optional<std::size_t>> slotsInFile() const {
    std::vector<std::pair<std::size_t, std::size_t>> found; // смещение, номер кадра
    for (std::size_t index = 0; index < shots.size(); ++index) {
      auto from = file->begin();
      for (;;) {
        const auto hit =
            std::search(from, file->end(), shots[index].begin(), shots[index].end());
        if (hit == file->end())
          break;
        found.emplace_back(static_cast<std::size_t>(hit - file->begin()), index);
        from = hit + 1;
      }
    }
    std::ranges::sort(found);
    std::vector<std::optional<std::size_t>> order;
    order.reserve(found.size());
    for (const auto &[offset, index] : found)
      order.emplace_back(index);
    return order;
  }
};

} // namespace

TEST_CASE("дубль: место k получает кадр, снятый, когда звучало время файла k / 30") {
  Recording recording;
  recording.planShots(3.0);
  auto take = recording.start();

  const long blocks = std::lround(2.0 / kBlockSeconds);
  recording.record(*take, 0, blocks);
  const auto samples = take->samples();

  // Кадры последних мест сняты после остановки, пока звучал хвост: они
  // ставятся по продолжению прямой.
  recording.tail();
  take.reset();

  recording.checkEverySlot(samples);
  CHECK(recording.reports.empty());
}

TEST_CASE("дубль: слышимая позиция, которую хост держит на начале, не сдвигает кадры") {
  Recording recording;
  recording.output = 0.2;
  recording.holdAtStart = true;
  recording.planShots(3.0);
  auto take = recording.start();

  recording.record(*take, 0, std::lround(2.0 / kBlockSeconds));
  const auto samples = take->samples();
  recording.tail();
  take.reset();

  recording.checkEverySlot(samples);
}

TEST_CASE("дубль: блоки аудиохука прошлой записи с теми же позициями не мешают") {
  // Прошлая запись с той же позиции кончилась за три секунды до новой: её
  // блоки ещё в истории аудиохука.
  Recording recording;
  const long blocks = std::lround(2.0 / kBlockSeconds);
  auto previous = recording.start();
  recording.record(*previous, 0, blocks);
  previous.reset();

  recording.file = std::make_shared<std::vector<std::uint8_t>>();
  recording.clockNow = kStart + 5.0;
  recording.planShots(3.0, 5.0);
  auto take = recording.start();
  recording.record(*take, 0, blocks, 5.0);
  const auto samples = take->samples();
  recording.tail();
  take.reset();

  recording.checkEverySlot(samples);
}

TEST_CASE("дубль: кадры паузы не попадают в файл, после паузы кадры снова на местах") {
  Recording recording;
  auto take = recording.start();

  // Две секунды записи, пауза 3 с, ещё две секунды. Камера снимает всё время.
  for (int i = 0; i < 7 * 30; ++i)
    recording.shotsPlanned.push_back(kStart + (i / 30.0) + 0.004 + kOutput);

  const long blocks = std::lround(2.0 / kBlockSeconds);
  recording.record(*take, 0, blocks);
  recording.record(*take, blocks, blocks, 3.0);
  take.reset();

  const auto order = recording.slotsInFile();
  REQUIRE(order.size() >= 119);

  // Кадры паузы сняты между 2 и 5 секундами.
  const auto pauseFirst = static_cast<std::size_t>(2.0 * 30.0) + 1;
  const auto pauseLast = static_cast<std::size_t>(5.0 * 30.0) - 1;
  for (const auto &index : order) {
    REQUIRE(index);
    CHECK_FALSE((*index > pauseFirst && *index < pauseLast));
  }
}

TEST_CASE("дубль: потеря камеры — одно сообщение и чёрный кадр до конца") {
  Recording recording;
  recording.planShots(1.0); // камера снимает только первую секунду
  auto take = recording.start();

  const long second = std::lround(1.0 / kBlockSeconds);
  recording.record(*take, 0, second);
  recording.loss = "device error: No such device";
  recording.record(*take, second, 2 * second);
  const auto samples = take->samples();
  take.reset();

  REQUIRE(recording.reports.size() == 1);
  CHECK(recording.reports[0].find("Integrated_Webcam_HD") != std::string::npos);
  CHECK(recording.reports[0].find("black frame") != std::string::npos);

  // Секунда кадров и секунда повтора последнего, дальше — чёрный.
  const auto slots =
      static_cast<std::size_t>(std::ceil(static_cast<double>(samples) / kSampleRate * 30.0));
  const std::size_t fromCamera = recording.slotsInFile().size();
  const std::size_t black = recording.count(cam::capture::compressBlackFrame(160, 90));
  CHECK(fromCamera >= 59);
  CHECK(fromCamera <= 61);
  CHECK(fromCamera + black == slots);
}

TEST_CASE("дубль: без камеры весь дубль чёрный, запись идёт") {
  Recording recording;
  auto take = recording.start(false);
  recording.record(*take, 0, std::lround(1.0 / kBlockSeconds));
  take.reset();

  REQUIRE(recording.reports.size() == 1);
  CHECK(recording.reports[0].find("not selected") != std::string::npos);
  CHECK(recording.file->size() > 1000);
}
