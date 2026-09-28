#include <doctest/doctest.h>

#include "cam/capture/device_registry.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using cam::capture::Backend;
using cam::capture::CameraInfo;
using cam::capture::CameraMode;
using cam::capture::Capture;
using cam::capture::CaptureError;
using cam::capture::Device;
using cam::capture::DeviceRegistry;
using cam::capture::DeviceState;
using cam::capture::Frame;

using namespace std::chrono_literals;

namespace {

/// Как ведёт себя камера после заданного числа кадров.
enum class Ending {
  Forever,     // шлёт кадры, пока её не закроют
  DeviceError, // ошибка устройства — как при отключении USB
  Silence,     // молчит — как зависшая камера
};

struct Script {
  int frames = 0;
  Ending ending = Ending::Forever;
  bool busy = false;
};

class FakeCapture : public Capture {
public:
  FakeCapture(const Script &script, std::atomic<int> &open) : script_(script), open_(open) {
    ++open_;
  }
  ~FakeCapture() override { --open_; }

  FakeCapture(const FakeCapture &) = delete;
  FakeCapture &operator=(const FakeCapture &) = delete;
  FakeCapture(FakeCapture &&) = delete;
  FakeCapture &operator=(FakeCapture &&) = delete;

  std::optional<Frame> next(std::chrono::milliseconds timeout) override {
    if (script_.ending != Ending::Forever && sent_ >= script_.frames) {
      if (script_.ending == Ending::DeviceError)
        throw CaptureError("device error: No such device");
      std::this_thread::sleep_for(timeout);
      return std::nullopt;
    }

    std::this_thread::sleep_for(5ms);
    Frame frame;
    frame.jpeg = {0xFF, 0xD8, 0xFF, 0xD9};
    frame.sequence = static_cast<std::uint64_t>(sent_++);
    return frame;
  }

private:
  Script script_;
  std::atomic<int> &open_;
  int sent_ = 0;
};

class FakeBackend : public Backend {
public:
  Script script;
  std::atomic<int> opened{0};
  std::atomic<int> openCaptures{0};

  std::vector<CameraInfo> list() override { return {}; }

  std::unique_ptr<Capture> open(const std::string & /*id*/,
                                const CameraMode & /*mode*/) override {
    ++opened;
    if (script.busy)
      throw CaptureError("busy");
    return std::make_unique<FakeCapture>(script, openCaptures);
  }
};

constexpr CameraMode kMode{
    .width = 640, .height = 480, .rateNumerator = 30, .rateDenominator = 1};

/// Быстрые пороги: секунда молчания в тестах — сто миллисекунд.
constexpr Device::Settings kFast{.silence = 100ms, .warmUp = 300ms, .poll = 20ms};

bool waitFor(const std::function<bool()> &condition) {
  for (int i = 0; i < 200; ++i) {
    if (condition())
      return true;
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

} // namespace

TEST_CASE("реестр: ошибка устройства помечает камеру потерянной") {
  FakeBackend backend;
  backend.script = {.frames = 10, .ending = Ending::DeviceError, .busy = false};
  DeviceRegistry registry(backend, kFast);

  const auto device = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  REQUIRE(waitFor([&] { return device->state() == DeviceState::Lost; }));
  CHECK(device->lostReason().find("device error") != std::string::npos);
  CHECK(device->name() == "Integrated_Webcam_HD");
}

TEST_CASE("реестр: молчание дольше порога помечает камеру потерянной") {
  FakeBackend backend;
  backend.script = {.frames = 10, .ending = Ending::Silence, .busy = false};
  DeviceRegistry registry(backend, kFast);

  const auto device = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  REQUIRE(waitFor([&] { return device->state() == DeviceState::Running; }));
  REQUIRE(waitFor([&] { return device->state() == DeviceState::Lost; }));
  CHECK(device->lostReason() == "no frames");
}

TEST_CASE("реестр: занятая камера — потеряна с причиной сразу") {
  FakeBackend backend;
  backend.script = {.frames = 0, .ending = Ending::Forever, .busy = true};
  DeviceRegistry registry(backend, kFast);

  const auto device = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  REQUIRE(waitFor([&] { return device->state() == DeviceState::Lost; }));
  CHECK(device->lostReason() == "busy");
}

TEST_CASE("реестр: две дорожки делят один захват, последняя закрывает камеру") {
  FakeBackend backend;
  DeviceRegistry registry(backend, kFast);

  auto first = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  auto second = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  CHECK(first == second);
  REQUIRE(waitFor([&] { return backend.openCaptures == 1; }));

  first.reset();
  CHECK(registry.find("by-id/camera"));
  CHECK(backend.openCaptures == 1);

  second.reset();
  CHECK_FALSE(registry.find("by-id/camera"));
  CHECK(backend.openCaptures == 0);
  CHECK(backend.opened == 1);
}

TEST_CASE("реестр: после потери новый arm открывает камеру заново") {
  FakeBackend backend;
  backend.script = {.frames = 3, .ending = Ending::DeviceError, .busy = false};
  DeviceRegistry registry(backend, kFast);

  const auto lost = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  REQUIRE(waitFor([&] { return lost->state() == DeviceState::Lost; }));

  backend.script.ending = Ending::Forever;
  const auto fresh = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  CHECK(fresh != lost);
  REQUIRE(waitFor([&] { return fresh->state() == DeviceState::Running; }));
  CHECK(backend.opened == 2);
}

TEST_CASE("реестр: получатель получает кадры, переполнение теряет старые") {
  FakeBackend backend;
  DeviceRegistry registry(backend, kFast);

  const auto device = registry.acquire("by-id/camera", "Integrated_Webcam_HD", kMode);
  const auto queue = device->subscribe(4);

  const auto frame = queue->pop(1000ms);
  REQUIRE(frame);
  CHECK(frame->jpeg.size() == 4);

  // Никто не забирает кадры — очередь не растёт выше предела.
  REQUIRE(waitFor([&] { return queue->dropped() > 3; }));
  device->unsubscribe(queue);
}
