#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_ShowConsoleMsg

#include "services.hpp"

#include "journal.hpp"

#include <reaper_plugin_functions.h>

#include "test_source.hpp"

// Захват есть только для Linux (design.md D9). На других ОС модуль
// собирается с пустышкой: формат, окно и чёрный дубль работают, камер нет.
#ifdef __linux__
#include "cam/capture_v4l2/v4l2.hpp"
#else
#include "cam/capture/unsupported_backend.hpp"
#endif

namespace cam::reaper {
namespace {

std::unique_ptr<Services> instance;

std::unique_ptr<capture::Backend> systemBackend() {
#if defined(__linux__)
  return std::make_unique<capture_v4l2::V4l2Backend>();
#elif defined(_WIN32)
  return std::make_unique<capture::UnsupportedBackend>("Windows");
#elif defined(__APPLE__)
  return std::make_unique<capture::UnsupportedBackend>("macOS");
#else
  return std::make_unique<capture::UnsupportedBackend>("this system");
#endif
}

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

void onMainTimer() {
  if (instance)
    instance->flushMessages();
}

} // namespace

Services::Services()
    : backend_(withTestSource(systemBackend())),
      registry_(std::make_unique<capture::DeviceRegistry>(*backend_)),
      clock_(std::make_unique<AudioClock>(hooks_)) {}

Services::~Services() {
  // Аудиохук — первым: пока он снят, звуковой поток не пишет в историю.
  clock_.reset();
  registry_.reset();
}

void Services::post(const std::string &message) {
  const std::scoped_lock lock(messagesMutex_);
  messages_.push_back("reaper-cam: " + message + "\n");
}

void Services::flushMessages() {
  std::vector<std::string> messages;
  {
    const std::scoped_lock lock(messagesMutex_);
    messages.swap(messages_);
  }

  for (const std::string &message : messages) {
    journal("console: {}", message.substr(0, message.size() - 1));
    ShowConsoleMsg(message.c_str());
  }
}

Services &services() { return *instance; }

void initServices() {
  instance = std::make_unique<Services>();

  // Без захвата расширение всё равно грузится: пусть пользователь узнает об
  // этом сразу, а не по чёрному дублю после записи.
  const std::string unsupported = instance->backend().unsupported();
  if (!unsupported.empty())
    instance->post(unsupported + " Camera tracks record black video.");
}

void shutdownServices() { instance.reset(); }

void registerMainTimer(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  hostRegister("timer", reinterpret_cast<void *>(onMainTimer));
}

void unregisterMainTimer() {
  if (hostRegister)
    hostRegister("-timer", reinterpret_cast<void *>(onMainTimer));
}

} // namespace cam::reaper
