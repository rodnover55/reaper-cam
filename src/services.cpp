#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_ShowConsoleMsg

#include "services.hpp"

#include "journal.hpp"

#include <reaper_plugin_functions.h>

#include "test_source.hpp"

#include "cam/capture_v4l2/v4l2.hpp"

namespace cam::reaper {
namespace {

std::unique_ptr<Services> instance;

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

void onMainTimer() {
  if (instance)
    instance->flushMessages();
}

} // namespace

Services::Services()
    : backend_(withTestSource(std::make_unique<capture_v4l2::V4l2Backend>())),
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

void initServices() { instance = std::make_unique<Services>(); }

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
