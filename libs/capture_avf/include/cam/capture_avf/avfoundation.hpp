#pragma once

// Камеры macOS: AVFoundation. Единственная библиотека, которая знает про ОС;
// наружу она отдаёт интерфейсы cam_capture. Заголовок на чистом C++: его
// включает модуль расширения, который собирается без Objective-C.

#include "cam/capture/camera.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cam::capture_avf {

class AvFoundationBackend : public capture::Backend {
public:
  /// Встроенные и внешние камеры с их именами и режимами MJPEG. Признак
  /// камеры — её уникальный идентификатор в AVFoundation: он переживает
  /// перезагрузку. Камера без MJPEG остаётся в списке с пустыми режимами —
  /// окно настроек скажет, что она не поддерживается.
  std::vector<capture::CameraInfo> list() override;

  /// Открывает камеру и получает её кадры MJPEG в собственном формате
  /// устройства, без разжатия. Без разрешения на камеру — ошибка устройства с
  /// подсказкой, где его дать.
  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const capture::CameraMode &mode) override;
};

} // namespace cam::capture_avf
