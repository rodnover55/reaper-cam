// Регистрация формата записи «видео с камеры» и рождение его экземпляров.

#include "format.hpp"

#include "format_config.hpp"
#include "journal.hpp"
#include "services.hpp"
#include "sink.hpp"
#include "views/format_config.hpp"

#include "cam/container/output.hpp"

#include <exception>
#include <span>
#include <string>

namespace cam::reaper {
namespace {

/// Указатель на `Register` хоста: при выгрузке `rec` уже не дают.
int (*hostRegister)(const char *name, void *infostruct) = nullptr;

/// Режим файла, когда камера не выбрана: дубль чёрный, но идёт.
constexpr capture::CameraMode kBlackMode{
    .width = 640, .height = 360, .rateNumerator = 30, .rateDenominator = 1};

std::span<const std::uint8_t> bytesOf(const void *cfg, int cfgLength) {
  if (!cfg || cfgLength <= 0)
    return {};
  return {static_cast<const std::uint8_t *>(cfg), static_cast<std::size_t>(cfgLength)};
}

bool isOurs(const void *cfg, int cfgLength) { return isFormatConfig(bytesOf(cfg, cfgLength)); }

unsigned int getFormat(const char **description) {
  if (description)
    *description = "Video (camera)";

  return kFormatCode;
}

const char *getExtension(const void *cfg, int cfgLength) {
  return isOurs(cfg, cfgLength) ? "mov" : nullptr;
}

HWND showConfig(const void *cfg, int cfgLength, HWND parent) {
  if (!isOurs(cfg, cfgLength))
    return nullptr;

  return views::showFormatConfig(cfg, cfgLength, parent);
}

PCM_sink *createSink(const char *fileName, void *cfg, int cfgLength, int channels,
                     int sampleRate, bool /*buildPeaks*/) {
  if (!isOurs(cfg, cfgLength) || !fileName || sampleRate <= 0)
    return nullptr;

  // Экземпляр создаётся всегда: отказ CreateSink срывает всю запись, звук тоже
  // (design.md D11). Недоступная камера — чёрный кадр и сообщение.
  const FormatConfig config = decodeConfig(bytesOf(cfg, cfgLength)).value_or(FormatConfig{});
  const bool chosen = !config.cameraId.empty() && config.mode.width > 0;

  Take::Settings settings;
  settings.sampleRate = sampleRate;
  settings.mode = chosen ? config.mode : kBlackMode;
  settings.cameraName = config.cameraName;

  Take::Acquire acquire;
  if (chosen) {
    acquire = [config]() -> std::unique_ptr<CameraFeed> {
      // Захват обычно уже идёт — его открыл arm (design.md D3); если нет или
      // камера после arm не открылась, экземпляр открывает её сам: её могли
      // освободить до Record.
      auto &registry = services().registry();
      auto device = registry.find(config.cameraId);
      if (!device || device->state() == capture::DeviceState::Lost)
        device = registry.acquire(config.cameraId, config.cameraName, config.mode);
      return feedOf(std::move(device));
    };
  }

  try {
    auto take = std::make_unique<Take>(settings, container::openFileOutput(fileName),
                                       services().hooks(), std::move(acquire),
                                       [](const std::string &text) { services().post(text); });
    return new Sink(fileName, channels, sampleRate, std::move(take));
  } catch (const std::exception &error) {
    // Файл не открылся — это уже не камера: так же сорвалась бы и запись WAV.
    services().post(std::string("cannot create video file: ") + error.what());
    return nullptr;
  }
}

pcmsink_register_ext_t registration{};

} // namespace

void registerFormat(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;

  registration.sink.GetFmt = getFormat;
  registration.sink.GetExtension = getExtension;
  registration.sink.ShowConfig = showConfig;
  registration.sink.CreateSink = createSink;

  const int status = hostRegister("pcmsink_ext", &registration);
  journal("Register pcmsink_ext -> {}", status);
}

void unregisterFormat() {
  if (hostRegister)
    hostRegister("-pcmsink_ext", &registration);
}

} // namespace cam::reaper
