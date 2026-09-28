#pragma once

// Дубль камеры: всё, что происходит с одним файлом записи (design.md D4, D5,
// D8). Заголовков хоста здесь нет — дубль проверяется тестами без REAPER, а
// экземпляр формата и аудиохук только передают ему числа.

#include "cam/capture/device_registry.hpp"
#include "cam/container/output.hpp"
#include "cam/container/writer.hpp"
#include "cam/timing/clock_bridge.hpp"
#include "cam/timing/frame_grid.hpp"
#include "cam/timing/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace cam::reaper {

/// Отметка блока аудиохука: когда и какую позицию хост обрабатывал.
struct HookBlock {
  /// Секунды монотонных часов — тех же, что у меток кадров камеры.
  double monotonic = 0.0;
  /// Позиция обработки блока (GetPlayPosition2Ex).
  double position = 0.0;
  /// Слышимая в этот момент позиция (GetPlayPositionEx): по разнице с позицией
  /// обработки дубль узнаёт задержку вывода.
  double heard = 0.0;
  /// Состояние транспорта (GetPlayStateEx): бит 4 — запись.
  int state = 0;
  int length = 0;
};

/// История блоков аудиохука за последние секунды. Пополняет её рабочий поток
/// аудиочасов, читают потоки писателей дублей.
class HookHistory {
public:
  void append(const HookBlock &block);

  /// Блоки с номерами от `from` и дальше, с их номерами.
  std::vector<std::pair<std::uint64_t, HookBlock>> since(std::uint64_t from) const;

  /// Номер следующего блока — тот, что получит очередной append.
  std::uint64_t next() const;

private:
  /// Сколько секунд помнить: писатель отстаёт от хука на доли секунды.
  static constexpr double kKeptSeconds = 30.0;

  mutable std::mutex mutex_;
  std::deque<HookBlock> blocks_;
  std::uint64_t first_ = 0;
};

/// Кадры камеры для дубля: очередь получателя у захвата и его состояние.
class CameraFeed {
public:
  virtual ~CameraFeed() = default;

  CameraFeed() = default;
  CameraFeed(const CameraFeed &) = delete;
  CameraFeed &operator=(const CameraFeed &) = delete;
  CameraFeed(CameraFeed &&) = delete;
  CameraFeed &operator=(CameraFeed &&) = delete;

  /// Следующий пришедший кадр, без ожидания.
  virtual std::optional<capture::Frame> pop() = 0;

  /// Причина, если камера потеряна; пусто, пока она работает.
  virtual std::optional<std::string> lost() const = 0;

  /// Для журнала: откуда время кадров и что пришло от камеры.
  virtual std::string describe() const { return {}; }
};

/// Лента кадров идущего захвата из реестра устройств.
std::unique_ptr<CameraFeed> feedOf(std::shared_ptr<capture::Device> device);

/// Блок экземпляра формата: сэмплы из WriteDoubles и позиция из
/// SETCURBLOCKTIME перед ним.
struct SinkBlock {
  std::int64_t firstSample = 0;
  int length = 0;
  double position = 0.0;
};

class Take {
public:
  struct Settings {
    int sampleRate = 44100;
    /// Режим файла: размер кадра и частота сетки.
    capture::CameraMode mode;
    /// Имя камеры для сообщений.
    std::string cameraName;
    /// Сколько время файла уходит вперёд за момент места, прежде чем место
    /// решается повтором прошлого кадра (design.md D4).
    double decisionLag = 0.1;
    /// Монотонные часы, секунды. В тестах — имитация.
    std::function<double()> clock;
  };

  using Acquire = std::function<std::unique_ptr<CameraFeed>()>;
  using Report = std::function<void(const std::string &)>;

  /// `acquire` достаёт захват камеры дорожки; пусто — камера не выбрана.
  /// `report` — сообщение пользователю, из любого потока.
  /// `startThread` ложно только в тестах: там шаги зовутся вручную.
  Take(const Settings &settings, std::unique_ptr<container::Output> output,
       const HookHistory &history, Acquire acquire, Report report, bool startThread = true);

  /// Останавливает поток писателя, дописывает места до конца записанного и
  /// закрывает файл.
  ~Take();

  Take(const Take &) = delete;
  Take &operator=(const Take &) = delete;
  Take(Take &&) = delete;
  Take &operator=(Take &&) = delete;

  /// Из WriteDoubles: без блокировок, выделения памяти и ожидания (design.md D8).
  void onBlock(const SinkBlock &block) noexcept;

  /// Один шаг писателя. `final` — запись кончилась: решаются все места.
  void step(bool final);

  std::int64_t samples() const noexcept { return samples_.load(std::memory_order_acquire); }

private:
  void run();
  void matchBlocks();
  void addHeard(const HookBlock &hook, double fileTime, bool started);
  /// `pastEnd` — запись кончилась: моменты после последней пары ставятся по
  /// продолжению прямой.
  timing::StreamTime fileTimeOf(double capture, bool pastEnd) const;
  double now() const;
  double settledFileTime() const;
  void finishTail();
  void takeFrames();
  void placeFrames(bool final);
  void writeSlots(const std::vector<timing::Slot> &slots);
  void watchDevice();
  const std::vector<std::uint8_t> &blackFrame();

  // Общее с потоками хоста: блоки из WriteDoubles и счёт сэмплов. Кольцо
  // выровнено по строке кэша и стоит первым — так меньше дыр в раскладке.
  timing::SpscRing<SinkBlock, 4096> blocks_;
  std::atomic<std::int64_t> samples_{0};

  Settings settings_;
  const HookHistory &history_;
  Acquire acquire_;
  Report report_;

  // Дальше — только поток писателя.
  std::unique_ptr<container::VideoWriter> writer_;
  /// Монотонное время → время файла, которое звучало (design.md D5).
  timing::ClockBridge heardBridge_;
  timing::FrameGrid grid_;
  double created_; // монотонный момент рождения экземпляра
  std::deque<SinkBlock> unmatched_;
  std::uint64_t hookCursor_ = 0;
  std::optional<double> heldHeard_; // слышимая позиция, которую хост держит после начала
  double streamEnd_ = 0.0;
  double matchedEnd_ = 0.0;
  double lastProcessed_ = 0.0; // монотонный момент последнего сопоставленного блока
  double decisionEnd_ = 0.0;

  std::unique_ptr<CameraFeed> feed_;
  std::deque<capture::Frame> pending_;
  std::map<std::uint64_t, std::vector<std::uint8_t>> placed_;
  std::uint64_t nextFrameId_ = 0;
  std::vector<std::uint8_t> black_;

  bool cursorPlaced_ = false;
  bool matchedAny_ = false;
  bool deviceTried_ = false;
  bool lossReported_ = false;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

} // namespace cam::reaper
