#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_GetMediaTrackInfo_Value
#define REAPERAPI_WANT_SetMediaTrackInfo_Value
#define REAPERAPI_WANT_GetTrackStateChunk
#define REAPERAPI_WANT_GetPlayStateEx

#include "arm_watch.hpp"

#include <reaper_plugin_functions.h>

#include "format_config.hpp"
#include "journal.hpp"
#include "services.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cam::reaper {
namespace {

/// Режим записи дорожки-камеры — «выход (моно)» (findings.md, вопрос 4).
constexpr double kOutputMono = 5.0;

/// Состояние дорожки с тяжёлыми FX бывает большим: буфер растёт до предела.
constexpr std::size_t kFirstChunkSize = std::size_t{64} * 1024;
constexpr std::size_t kLargestChunkSize = std::size_t{64} * 1024 * 1024;

std::optional<std::string> chunkOf(MediaTrack *track) {
  for (std::size_t size = kFirstChunkSize; size <= kLargestChunkSize; size *= 4) {
    std::string chunk(size, '\0');
    if (!GetTrackStateChunk(track, chunk.data(), static_cast<int>(size), false))
      return std::nullopt;

    const std::size_t length = chunk.find('\0');
    if (length + 1 < size) { // влезло целиком
      chunk.resize(length);
      return chunk;
    }
  }
  return std::nullopt;
}

class ArmWatch : public IReaperControlSurface {
public:
  const char *GetTypeString() override { return "REAPERCAM"; }
  const char *GetDescString() override { return "reaper-cam: camera warm-up on arm"; }
  const char *GetConfigString() override { return ""; }

  // Arm — повод заново открыть потерянную камеру: её могли освободить или
  // подключить (camera-capture). Смена списка дорожек — нет: иначе занятая
  // камера открывалась бы и сообщала о себе при каждой правке проекта.
  void SetTrackListChange() override { refresh(false); }
  void SetSurfaceRecArm(MediaTrack * /*trackid*/, bool /*recarm*/) override { refresh(true); }

  /// Около 30 раз в секунду: камера, которая не открылась после arm,
  /// сообщает о себе один раз (design.md D11).
  void Run() override { reportLost(); }

  void releaseAll() { held_.clear(); }

private:
  struct Held {
    std::shared_ptr<capture::Device> device;
    bool reported = false;
  };

  void refresh(bool retryLost);
  void reportLost();

  /// Захваты поставленных на запись дорожек-камер: пока указатель здесь,
  /// камера включена (реестр устройств, design.md D3).
  std::vector<Held> held_;
};

void ArmWatch::reportLost() {
  // Во время записи о потере сообщает дубль — одно сообщение на дубль
  // (design.md D11); здесь такая потеря только отмечается.
  constexpr int kRecording = 4;
  const bool recording = (GetPlayStateEx(nullptr) & kRecording) != 0;

  for (Held &entry : held_) {
    if (entry.reported || entry.device->state() != capture::DeviceState::Lost)
      continue;

    entry.reported = true;
    if (!recording)
      services().post("camera \"" + entry.device->name() + "\" is not available (" +
                      entry.device->lostReason() + ")");
  }
}

void ArmWatch::refresh(bool retryLost) {
  std::vector<Held> held;

  const int count = CountTracks(nullptr);
  for (int index = 0; index < count; ++index) {
    MediaTrack *track = GetTrack(nullptr, index);
    if (!track || GetMediaTrackInfo_Value(track, "I_RECARM") == 0.0)
      continue;

    // Формат записи читается только у поставленных на запись дорожек.
    const auto chunk = chunkOf(track);
    const auto bytes = chunk ? recordConfigOf(*chunk) : std::nullopt;
    if (!bytes || !isFormatConfig(*bytes))
      continue;

    // Дорожка-камера пишет выход без мониторинга: ручной работы нет.
    if (GetMediaTrackInfo_Value(track, "I_RECMODE") != kOutputMono)
      SetMediaTrackInfo_Value(track, "I_RECMODE", kOutputMono);
    if (GetMediaTrackInfo_Value(track, "I_RECMON") != 0.0)
      SetMediaTrackInfo_Value(track, "I_RECMON", 0.0);

    const FormatConfig config = decodeConfig(*bytes).value_or(FormatConfig{});
    if (config.cameraId.empty() || config.mode.width <= 0)
      continue; // камера не выбрана: дубль будет чёрным, включать нечего

    // Потерянный захват той же камеры остаётся как есть, пока нет нового arm:
    // сообщение о нём уже было.
    const auto kept = std::ranges::find_if(
        held_, [&](const Held &entry) { return entry.device->id() == config.cameraId; });
    if (kept != held_.end() &&
        (!retryLost || kept->device->state() != capture::DeviceState::Lost)) {
      held.push_back(*kept);
      continue;
    }

    held.push_back(Held{.device = services().registry().acquire(
                            config.cameraId, config.cameraName, config.mode),
                        .reported = false});
  }

  // Новые захваты взяты раньше, чем отпущены старые: камера, которая нужна и
  // дальше, не перезапускается.
  if (held.size() != held_.size())
    journal("arm watch: cameras held {} -> {}", held_.size(), held.size());
  held_.swap(held);
}

int (*hostRegister)(const char *name, void *infostruct) = nullptr;
std::unique_ptr<ArmWatch> watch;

} // namespace

void registerArmWatch(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  watch = std::make_unique<ArmWatch>();
  hostRegister("csurf_inst", watch.get());
}

void unregisterArmWatch() {
  if (!watch)
    return;

  if (hostRegister)
    hostRegister("-csurf_inst", watch.get());
  watch->releaseAll();
  watch.reset();
}

} // namespace cam::reaper
