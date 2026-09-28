#pragma once

#include "cam/capture/camera.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace cam::capture {

/// Очередь кадров одного получателя. Ограничена: если получатель отстаёт,
/// теряются старые кадры видео, а не что-то ещё (design.md D8).
class FrameQueue {
public:
  explicit FrameQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(const Frame &frame);

  /// Следующий кадр, если он пришёл за `timeout`.
  std::optional<Frame> pop(std::chrono::milliseconds timeout);

  /// Сколько кадров выброшено из-за переполнения.
  std::size_t dropped() const;

private:
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Frame> frames_;
  std::size_t dropped_ = 0;
};

/// Состояние захвата камеры.
enum class DeviceState {
  /// Камера открывается или ещё не прислала первый кадр.
  Starting,
  /// Кадры идут.
  Running,
  /// Камера недоступна: занята, отключена, замолчала. Захват остановлен.
  Lost,
};

/// Идущий захват одной камеры: поток захвата и получатели кадров. Живёт,
/// пока его держит хотя бы одна дорожка-камера (design.md D3).
class Device {
public:
  struct Settings {
    /// Молчание дольше этого — камера потеряна (`camera-recording`).
    std::chrono::milliseconds silence{1000};
    /// Столько камере даётся на первый кадр: прогрев длиннее обычной паузы.
    std::chrono::milliseconds warmUp{3000};
    /// Как часто поток захвата просыпается проверить молчание и остановку.
    std::chrono::milliseconds poll{100};
  };

  Device(Backend &backend, std::string id, std::string name, CameraMode mode,
         Settings settings);
  ~Device();

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&) = delete;
  Device &operator=(Device &&) = delete;

  const std::string &id() const { return id_; }
  const std::string &name() const { return name_; }
  const CameraMode &mode() const { return mode_; }

  DeviceState state() const;

  /// Почему камера потеряна; пусто, пока она не потеряна.
  std::string lostReason() const;

  /// Получатель кадров: кадры, пришедшие после подписки, идут в его очередь.
  std::shared_ptr<FrameQueue> subscribe(std::size_t capacity);
  void unsubscribe(const std::shared_ptr<FrameQueue> &queue);

  /// Для журнала: что захват сообщает о себе (Capture::describe).
  std::string describe() const;

private:
  void run(Backend &backend);
  void lose(const std::string &reason);
  void deliver(const Frame &frame);

  std::string id_;
  std::string name_;
  CameraMode mode_;
  Settings settings_;

  mutable std::mutex mutex_;
  DeviceState state_ = DeviceState::Starting;
  std::string lostReason_;
  std::string description_;
  std::vector<std::shared_ptr<FrameQueue>> queues_;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

/// Реестр идущих захватов: один захват на камеру. Первая дорожка-камера
/// открывает камеру, последняя закрывает, две дорожки с одной камерой делят
/// один захват (design.md D3).
class DeviceRegistry {
public:
  explicit DeviceRegistry(Backend &backend, Device::Settings settings = {});

  /// Захват камеры `id`. Держатель — возвращённый указатель: пока жив хоть
  /// один, камера включена. Уже идущий захват делится — в своём режиме; если
  /// он потерян, открывается новый.
  std::shared_ptr<Device> acquire(const std::string &id, const std::string &name,
                                  const CameraMode &mode);

  /// Идущий захват камеры, если его кто-то держит.
  std::shared_ptr<Device> find(const std::string &id) const;

private:
  Backend &backend_;
  Device::Settings settings_;
  mutable std::mutex mutex_;
  std::map<std::string, std::weak_ptr<Device>> devices_;
};

} // namespace cam::capture
