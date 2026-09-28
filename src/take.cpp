#include "take.hpp"

#include "journal.hpp"

#include "cam/capture/jpeg_encoder.hpp"
#include "cam/container/jpeg_tables.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <utility>

namespace cam::reaper {
namespace {

/// Биты состояния транспорта хоста: запись и пауза. На паузе записи стоят
/// оба, и такие блоки не записываются.
constexpr int kRecording = 4;
constexpr int kPaused = 2;

bool recording(int state) { return (state & kRecording) != 0 && (state & kPaused) == 0; }

/// Позиции блока у экземпляра и у аудиохука — одно и то же число
/// (findings.md, вопрос 7); допуск — на округление.
constexpr double kSamePosition = 1e-7;

/// Если блок экземпляра так и не нашёлся среди стольких блоков аудиохука, он
/// выбрасывается: иначе поиск ходил бы по растущей истории вечно.
constexpr std::size_t kGiveUpAfterHooks = 2000;

/// Блоки аудиохука раньше рождения экземпляра на столько секунд — чужие: они
/// от прошлой записи, и их позиции совпали бы с позициями нового дубля.
constexpr double kBeforeCreation = 0.5;

/// Разница «позиция обработки − слышимая позиция» правдоподобна в этих
/// пределах; вне их — переход петли или откат после паузы, а не задержка.
constexpr double kLongestLead = 1.0;

/// Очередь кадров камеры у писателя: около двух секунд при 30 кадрах.
constexpr std::size_t kFrameQueue = 64;

/// Как часто писатель просыпается.
constexpr auto kStepPeriod = std::chrono::milliseconds(10);

/// Кадр доходит от съёмки до писателя за несколько миллисекунд: столько
/// хвост ждёт сверх задержки вывода.
constexpr double kDeliveryMargin = 0.03;

/// При остановке кадры хвоста ещё снимаются — их звук ещё звучит. Писатель
/// ждёт их не дольше задержки вывода с запасом и не дольше этого.
constexpr double kLongestTailWait = 0.3;

class DeviceFeed : public CameraFeed {
public:
  explicit DeviceFeed(std::shared_ptr<capture::Device> device)
      : device_(std::move(device)), queue_(device_->subscribe(kFrameQueue)) {}

  ~DeviceFeed() override { device_->unsubscribe(queue_); }

  DeviceFeed(const DeviceFeed &) = delete;
  DeviceFeed &operator=(const DeviceFeed &) = delete;
  DeviceFeed(DeviceFeed &&) = delete;
  DeviceFeed &operator=(DeviceFeed &&) = delete;

  std::optional<capture::Frame> pop() override {
    return queue_->pop(std::chrono::milliseconds(0));
  }

  std::optional<std::string> lost() const override {
    if (device_->state() != capture::DeviceState::Lost)
      return std::nullopt;
    return device_->lostReason();
  }

  std::string describe() const override { return device_->describe(); }

private:
  std::shared_ptr<capture::Device> device_;
  std::shared_ptr<capture::FrameQueue> queue_;
};

} // namespace

std::unique_ptr<CameraFeed> feedOf(std::shared_ptr<capture::Device> device) {
  if (!device)
    return nullptr;
  return std::make_unique<DeviceFeed>(std::move(device));
}

void HookHistory::append(const HookBlock &block) {
  const std::scoped_lock lock(mutex_);
  blocks_.push_back(block);
  while (blocks_.size() > 1 && blocks_.front().monotonic < block.monotonic - kKeptSeconds) {
    blocks_.pop_front();
    ++first_;
  }
}

std::vector<std::pair<std::uint64_t, HookBlock>> HookHistory::since(std::uint64_t from) const {
  const std::scoped_lock lock(mutex_);
  std::vector<std::pair<std::uint64_t, HookBlock>> out;

  const std::uint64_t start = std::max(from, first_);
  for (std::uint64_t index = start; index < first_ + blocks_.size(); ++index)
    out.emplace_back(index, blocks_[static_cast<std::size_t>(index - first_)]);

  return out;
}

std::uint64_t HookHistory::next() const {
  const std::scoped_lock lock(mutex_);
  return first_ + blocks_.size();
}

Take::Take(const Settings &settings, std::unique_ptr<container::Output> output,
           const HookHistory &history, Acquire acquire, Report report, bool startThread)
    : settings_(settings), history_(history), acquire_(std::move(acquire)),
      report_(std::move(report)),
      writer_(container::createWriter(
          std::move(output),
          container::VideoFormat{.width = settings.mode.width,
                                 .height = settings.mode.height,
                                 .rateNumerator = settings.mode.rateNumerator,
                                 .rateDenominator = settings.mode.rateDenominator})),
      grid_(timing::FrameGrid::Settings{
          .rate = timing::Rate{.numerator = settings.mode.rateNumerator,
                               .denominator = settings.mode.rateDenominator},
          .decisionLag = settings.decisionLag,
          .gapLimit = 1.0}),
      created_(now()) {
  if (startThread)
    thread_ = std::thread([this] { run(); });
}

Take::~Take() {
  stop_.store(true);
  if (thread_.joinable())
    thread_.join();

  try {
    finishTail();
    step(true);
    writer_->finish();
    if (feed_)
      journal("take camera: {}", feed_->describe());
  } catch (const std::exception &error) {
    report_(std::string("video file was not finished: ") + error.what());
  }
}

double Take::now() const {
  if (settings_.clock)
    return settings_.clock();
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

timing::StreamTime Take::fileTimeOf(double capture, bool pastEnd) const {
  // Дорожка-камера пишет выход (design.md D3): записанное стоит там, где
  // обрабатывалось, и кадру достаётся время файла, которое звучало в момент
  // съёмки (design.md D5).
  timing::StreamTime time = heardBridge_.streamAt(capture);
  if (time.coverage == timing::Coverage::NotYet && pastEnd) {
    if (const auto seconds = heardBridge_.extrapolate(capture))
      time = {.coverage = timing::Coverage::Known, .seconds = *seconds};
  }
  return time;
}

double Take::settledFileTime() const {
  // Время файла, которое звучало в последний момент с устоявшимся переводом:
  // кадры, снятые раньше, уже поставлены, а кадры мест позже него ещё ждут —
  // или ещё не сняты, потому что их звук не прозвучал.
  const auto until = heardBridge_.settledUntil();
  if (!until)
    return decisionEnd_;

  const auto heard = heardBridge_.streamAt(*until);
  return heard.coverage == timing::Coverage::Known ? heard.seconds : decisionEnd_;
}

void Take::finishTail() {
  // Обработанное звучит ещё задержку вывода после остановки, и камера снимает
  // кадры последних мест: писатель ждёт момента, когда прозвучал бы конец
  // файла, — по продолжению прямой моста, с запасом на полкадра и доставку, но
  // не дольше kLongestTailWait: ждёт главный поток хоста.
  if (!feed_ || settings_.clock)
    return;

  step(false); // последние блоки записи
  const auto heard = heardBridge_.extrapolate(lastProcessed_);
  if (!heard)
    return;

  const double halfFrame =
      settings_.mode.rateDenominator / (2.0 * settings_.mode.rateNumerator);
  const double endHeard = lastProcessed_ + (streamEnd_ - *heard);
  const double deadline =
      std::min(endHeard + halfFrame + kDeliveryMargin, now() + kLongestTailWait);
  journal("take tail: now={:.3f} end heard={:.3f} deadline={:.3f}", now(), endHeard, deadline);
  while (now() < deadline) {
    takeFrames();
    std::this_thread::sleep_for(kStepPeriod);
  }
}

void Take::onBlock(const SinkBlock &block) noexcept {
  (void)blocks_.push(block);
  samples_.store(block.firstSample + block.length, std::memory_order_release);
}

void Take::run() {
  while (!stop_.load()) {
    try {
      step(false);
    } catch (const std::exception &error) {
      report_(std::string("video recording stopped: ") + error.what());
      return;
    }
    std::this_thread::sleep_for(kStepPeriod);
  }
}

void Take::step(bool final) {
  while (const auto block = blocks_.pop()) {
    unmatched_.push_back(*block);
    streamEnd_ =
        static_cast<double>(block->firstSample + block->length) / settings_.sampleRate;
  }

  // Дубль начинается с первого блока: экземпляр, созданный без записи, камеру
  // не открывает (findings.md — экземпляр без записи).
  if (!unmatched_.empty() && !deviceTried_) {
    deviceTried_ = true;
    feed_ = acquire_ ? acquire_() : nullptr;
    if (!feed_)
      report_("camera is not selected: the take is black");
  }

  matchBlocks();
  watchDevice();
  takeFrames();
  placeFrames(final);

  // Места решаются только до времени файла с устоявшимся переводом: кадр
  // места снимается, когда его звук звучит, — на задержку вывода позже
  // обработки, и ставится, когда пары после него заполнили окно моста. У
  // PulseAudio обработка уходит вперёд пачками, и решать по ней — значит
  // повторять кадры, которых ещё нет (findings.md — задержка вывода). В конце
  // записи решаются все.
  if (!final)
    decisionEnd_ = std::max(decisionEnd_, std::min(matchedEnd_, settledFileTime()));

  grid_.advance(final ? streamEnd_ : decisionEnd_);
  writeSlots(final ? grid_.finish() : grid_.take());
}

void Take::matchBlocks() {
  if (unmatched_.empty())
    return;

  // Первый блок дубля обработан аудиохуком раньше, чем экземпляр его получил:
  // поиск начинается с блоков от рождения экземпляра.
  if (!cursorPlaced_) {
    const auto all = history_.since(0);
    const auto first = std::ranges::find_if(all, [this](const auto &hook) {
      return hook.second.monotonic >= created_ - kBeforeCreation;
    });
    if (first != all.end())
      hookCursor_ = first->first;
    else if (!all.empty())
      hookCursor_ = all.back().first + 1;
    cursorPlaced_ = true;
  }

  const auto hooks = history_.since(hookCursor_);
  std::size_t at = 0;

  while (!unmatched_.empty()) {
    const SinkBlock &block = unmatched_.front();

    // Следующий блок записи аудиохука с той же позицией: по порядку, потому
    // что позиции повторяются в петле и откатываются после паузы
    // (design.md D5). Блоки без записи между ними — начало или пауза.
    std::size_t found = at;
    bool started = !matchedAny_;
    while (found < hooks.size() &&
           (!recording(hooks[found].second.state) ||
            std::abs(hooks[found].second.position - block.position) > kSamePosition)) {
      started = started || !recording(hooks[found].second.state);
      ++found;
    }

    if (found == hooks.size()) {
      if (hooks.size() - at > kGiveUpAfterHooks)
        unmatched_.pop_front(); // так и не нашёлся — без пары
      break;
    }

    const HookBlock &hook = hooks[found].second;
    addHeard(hook, static_cast<double>(block.firstSample) / settings_.sampleRate, started);

    matchedAny_ = true;
    matchedEnd_ = static_cast<double>(block.firstSample + block.length) / settings_.sampleRate;
    lastProcessed_ = hook.monotonic;
    hookCursor_ = hooks[found].first + 1;
    at = found + 1;
    unmatched_.pop_front();
  }
}

void Take::addHeard(const HookBlock &hook, double fileTime, bool started) {
  // После начала записи и после паузы хост держит слышимую позицию на месте,
  // пока звук записи не дошёл до выхода, а обработка уже идёт: такие пары —
  // ожидание, а не задержка (findings.md — задержка вывода).
  if (started)
    heldHeard_ = hook.heard;
  if (heldHeard_ && std::abs(hook.heard - *heldHeard_) > kSamePosition)
    heldHeard_.reset();
  if (heldHeard_)
    return;

  // Что звучало в момент обработки блока: время файла блока минус то, на
  // сколько обработка опередила слышимое. Слышимую позицию хост ведёт по
  // своим задержкам — это и есть «по значениям, которые сообщает REAPER»
  // (recording-sync). В петле слышимая позиция может быть ещё в прошлом
  // проходе: такие разницы не задержка и отбрасываются.
  const double lead = hook.position - hook.heard;
  if (lead < 0.0 || lead > kLongestLead)
    return;

  heardBridge_.add(hook.monotonic, fileTime - lead);
  journal("pair t={:.6f} file={:.6f} p={:.6f} h={:.6f}", hook.monotonic, fileTime,
          hook.position, hook.heard);
}

void Take::watchDevice() {
  if (!feed_ || lossReported_)
    return;

  const auto reason = feed_->lost();
  if (!reason)
    return;

  lossReported_ = true;
  report_("camera \"" + settings_.cameraName + "\" is not available (" + *reason +
          "): black frame until the end of the take");
}

void Take::takeFrames() {
  if (!feed_)
    return;

  while (auto frame = feed_->pop())
    pending_.push_back(std::move(*frame));
}

void Take::placeFrames(bool final) {
  // Кадр ставится, когда перевод его момента устоялся: пары после него
  // заполнили окно моста. В конце записи пар больше не будет: кадры хвоста,
  // снятые после последнего блока, пока звучало обработанное, ставятся по
  // продолжению прямой.
  while (!pending_.empty()) {
    const capture::Frame &frame = pending_.front();
    if (!final && !heardBridge_.settled(frame.captureTime))
      break;

    const auto time = fileTimeOf(frame.captureTime, final);
    if (time.coverage == timing::Coverage::NotYet)
      break; // мост ещё не дошёл до этого момента

    if (time.coverage == timing::Coverage::Known) {
      journal("place capture={:.6f} file={:.6f} final={}", frame.captureTime, time.seconds,
              final);
      grid_.addFrame(nextFrameId_, time.seconds);
      placed_.emplace(nextFrameId_, std::move(pending_.front().jpeg));
      ++nextFrameId_;
    }
    // Разрыв: кадр снят до записи или в паузе — в файл он не попадает.

    pending_.pop_front();
  }
}

const std::vector<std::uint8_t> &Take::blackFrame() {
  if (black_.empty())
    black_ = capture::compressBlackFrame(settings_.mode.width, settings_.mode.height);
  return black_;
}

void Take::writeSlots(const std::vector<timing::Slot> &slots) {
  std::optional<std::uint64_t> lastUsed;

  for (const timing::Slot &slot : slots) {
    const auto frame = slot.frame ? placed_.find(*slot.frame) : placed_.end();
    if (frame != placed_.end()) {
      writer_->append(container::withHuffmanTables(frame->second));
      lastUsed = frame->first;
    } else {
      writer_->append(blackFrame());
    }
  }

  // Кадры до последнего взятого уже не понадобятся: места идут вперёд.
  if (lastUsed)
    placed_.erase(placed_.begin(), placed_.lower_bound(*lastUsed));
}

} // namespace cam::reaper
