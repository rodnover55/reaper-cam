#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_GetPlayStateEx

#include "audio_clock.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"

#include <chrono>

namespace cam::reaper {

AudioClock::AudioClock(HookHistory &history) : history_(history) {
  hook_.OnAudioBuffer = onAudioBuffer;
  hook_.userdata1 = this;
  registered_ = Audio_RegHardwareHook(true, &hook_) > 0;

  worker_ = std::thread([this] {
    while (!stop_.load()) {
      drain();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
}

AudioClock::~AudioClock() {
  if (registered_)
    Audio_RegHardwareHook(false, &hook_);

  stop_.store(true);
  if (worker_.joinable())
    worker_.join();
}

void AudioClock::onAudioBuffer(bool isPost, int length, double /*sampleRate*/,
                               audio_hook_register_t *registration) {
  // Звуковой поток: только монотонные часы и запись в кольцо — ни блокировок,
  // ни выделения памяти (design.md D5).
  if (isPost || !registration || !registration->userdata1)
    return;

  auto *self = static_cast<AudioClock *>(registration->userdata1);
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  (void)self->ring_.push({.monotonic = std::chrono::duration<double>(now).count(),
                          .position = GetPlayPosition2Ex(nullptr),
                          .heard = GetPlayPositionEx(nullptr),
                          .state = GetPlayStateEx(nullptr),
                          .length = length});
}

void AudioClock::drain() {
  while (const auto block = ring_.pop()) {
    history_.append(*block);
    // Отладочная сборка: отметки блоков записи — для сверки моста часов.
    if ((block->state & 4) != 0)
      journal("hook t={:.6f} p={:.6f} h={:.6f} s={}", block->monotonic, block->position,
              block->heard, block->state);
  }
}

} // namespace cam::reaper
