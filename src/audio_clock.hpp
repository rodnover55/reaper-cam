#pragma once

// Аудиочасы (design.md D5): аудиохук отмечает каждый блок, рабочий поток
// переносит отметки в историю, по которой дубли строят мост часов.

#include "take.hpp"

#include "cam/timing/spsc_ring.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

#include <atomic>
#include <cstddef>
#include <thread>

namespace cam::reaper {

class AudioClock {
public:
  explicit AudioClock(HookHistory &history);

  /// Снимает аудиохук и останавливает рабочий поток.
  ~AudioClock();

  AudioClock(const AudioClock &) = delete;
  AudioClock &operator=(const AudioClock &) = delete;
  AudioClock(AudioClock &&) = delete;
  AudioClock &operator=(AudioClock &&) = delete;

  /// Сколько отметок потеряно: рабочий поток не успевал.
  std::size_t dropped() const { return ring_.dropped(); }

private:
  static void onAudioBuffer(bool isPost, int length, double sampleRate,
                            audio_hook_register_t *registration);
  void drain();

  timing::SpscRing<HookBlock, 8192> ring_;
  HookHistory &history_;
  std::thread worker_;
  audio_hook_register_t hook_{};
  bool registered_ = false;
  std::atomic<bool> stop_{false};
};

} // namespace cam::reaper
