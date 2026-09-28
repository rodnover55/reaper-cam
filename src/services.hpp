#pragma once

// Службы расширения, общие для всех дорожек-камер: камеры системы, реестр
// идущих захватов, аудиочасы и сообщения пользователю. Создаются при загрузке
// расширения и живут до выгрузки.

#include "audio_clock.hpp"
#include "take.hpp"

#include "cam/capture/device_registry.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cam::reaper {

class Services {
public:
  Services();
  ~Services();

  Services(const Services &) = delete;
  Services &operator=(const Services &) = delete;
  Services(Services &&) = delete;
  Services &operator=(Services &&) = delete;

  capture::Backend &backend() { return *backend_; }
  capture::DeviceRegistry &registry() { return *registry_; }
  HookHistory &hooks() { return hooks_; }
  AudioClock &clock() { return *clock_; }

  /// Сообщение в консоль REAPER — из любого потока: выводит его главный
  /// поток по таймеру.
  void post(const std::string &message);

  /// Главный поток: вывести накопленные сообщения.
  void flushMessages();

private:
  std::unique_ptr<capture::Backend> backend_;
  std::unique_ptr<capture::DeviceRegistry> registry_;
  HookHistory hooks_;
  std::unique_ptr<AudioClock> clock_;

  std::mutex messagesMutex_;
  std::vector<std::string> messages_;
};

/// Службы расширения; есть между initServices и shutdownServices.
Services &services();

void initServices();
void shutdownServices();

/// Таймер главного потока: выводит сообщения служб в консоль.
void registerMainTimer(reaper_plugin_info_t *rec);
void unregisterMainTimer();

} // namespace cam::reaper
