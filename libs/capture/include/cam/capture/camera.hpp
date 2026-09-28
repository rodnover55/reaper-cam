#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cam::capture {

/// Режим камеры: размер кадра и частота кадров дробью (30/1, 30000/1001).
/// Предлагаются только режимы MJPEG (`camera-capture`).
struct CameraMode {
  int width = 0;
  int height = 0;
  int rateNumerator = 30;
  int rateDenominator = 1;

  auto operator<=>(const CameraMode &) const = default;

  double framesPerSecond() const {
    return static_cast<double>(rateNumerator) / rateDenominator;
  }
};

/// Камера системы, которая отдаёт видео.
struct CameraInfo {
  /// Устойчивый признак камеры: переживает перезагрузку и смену номеров
  /// устройств (design.md D2).
  std::string id;
  /// Имя, которое сообщает система.
  std::string name;
  /// Режимы MJPEG. Пусто — камера MJPEG не отдаёт и не поддерживается.
  std::vector<CameraMode> modes;
};

/// Кадр камеры: сжатые данные как пришли и время съёмки.
struct Frame {
  std::vector<std::uint8_t> jpeg;
  /// Начало экспозиции в секундах монотонных часов — тех же, что у аудиохука
  /// (design.md D5). Если камера время не сообщает — метка буфера системы.
  double captureTime = 0.0;
  /// Время взято от камеры, а не от системы (design.md D6).
  bool timeFromCamera = false;
  /// Метка буфера системы — когда кадр пришёл, в тех же монотонных часах.
  double systemTime = 0.0;
  std::uint64_t sequence = 0;
};

/// Ошибка устройства: камера занята, отключена, режим не принят.
class CaptureError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// Идущий захват одной камеры в одном режиме.
class Capture {
public:
  virtual ~Capture() = default;

  Capture() = default;
  Capture(const Capture &) = delete;
  Capture &operator=(const Capture &) = delete;
  Capture(Capture &&) = delete;
  Capture &operator=(Capture &&) = delete;

  /// Следующий кадр, если он пришёл за `timeout`. Ошибка устройства —
  /// исключение `CaptureError`.
  virtual std::optional<Frame> next(std::chrono::milliseconds timeout) = 0;

  /// Для журнала: откуда берётся время кадров и что пришло от камеры.
  virtual std::string describe() const { return {}; }
};

/// Камеры одной ОС: перечень и открытие. От ОС зависит только он
/// (design.md D9).
class Backend {
public:
  virtual ~Backend() = default;

  Backend() = default;
  Backend(const Backend &) = delete;
  Backend &operator=(const Backend &) = delete;
  Backend(Backend &&) = delete;
  Backend &operator=(Backend &&) = delete;

  /// Камеры, которые сейчас есть в системе. Список составляется заново при
  /// каждом вызове.
  virtual std::vector<CameraInfo> list() = 0;

  /// Открывает камеру `id` в режиме `mode` монопольно и запускает захват.
  virtual std::unique_ptr<Capture> open(const std::string &id, const CameraMode &mode) = 0;
};

} // namespace cam::capture
