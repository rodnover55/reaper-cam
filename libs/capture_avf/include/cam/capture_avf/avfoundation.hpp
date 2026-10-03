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
  /// Встроенные и внешние камеры и iPhone как камера, с их именами и
  /// режимами. Признак камеры — её уникальный идентификатор в AVFoundation:
  /// он переживает перезагрузку. Режимы — все форматы камеры: MJPEG пишется
  /// как есть, остальное при захвате сжимает в JPEG VideoToolbox.
  std::vector<capture::CameraInfo> list() override;

  /// Открывает камеру и отдаёт её кадры в JPEG: MJPEG — как пришёл, картинку
  /// — сжатой. Без разрешения на камеру — ошибка устройства с подсказкой, где
  /// его дать.
  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const capture::CameraMode &mode) override;
};

} // namespace cam::capture_avf
