#pragma once

// Выбор камеры и режима в окне настроек формата (camera-capture): что окно
// показывает и какую конфигурацию отдаёт хосту. Заголовков хоста здесь нет —
// правила выбора проверяются тестами без REAPER, а окно только рисует их.

#include "format_config.hpp"

#include "cam/capture/camera.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cam::reaper {

class FormatChoice {
public:
  struct Camera {
    capture::CameraInfo info;
    /// Ложь — камеры нет в системе, а дорожка её помнит.
    bool connected = true;
  };

  /// `cameras` — камеры системы сейчас, `saved` — конфигурация дорожки.
  /// Сохранённая камера, которой нет в системе, остаётся в списке с
  /// сохранённым режимом: выбор не теряется. Если камера не сохранена,
  /// выбирается первая с MJPEG и её первый режим.
  FormatChoice(std::vector<capture::CameraInfo> cameras, FormatConfig saved);

  const std::vector<Camera> &cameras() const { return cameras_; }
  std::optional<std::size_t> camera() const { return camera_; }
  std::optional<std::size_t> mode() const { return mode_; }

  /// Выбор пользователя. Ложь — камера без MJPEG: выбрать её нельзя, выбор
  /// остаётся прежним, а `status` объясняет почему.
  bool chooseCamera(std::size_t index);
  void chooseMode(std::size_t index);

  /// Конфигурация для хоста. Камера не выбрана — пустая: дубль будет чёрным.
  FormatConfig config() const;

  /// Строка под списками; пусто — сообщать нечего.
  std::string status() const;

private:
  std::vector<Camera> cameras_;
  std::optional<std::size_t> camera_;
  std::optional<std::size_t> mode_;
  std::string notice_;
};

/// Подпись камеры в списке: имя от системы и, если надо, почему её нельзя
/// записывать.
std::string cameraLabel(const FormatChoice::Camera &camera);

/// Подпись режима: «1280x720, 30 fps», дробная частота — «29.97 fps».
std::string modeLabel(const capture::CameraMode &mode);

} // namespace cam::reaper
