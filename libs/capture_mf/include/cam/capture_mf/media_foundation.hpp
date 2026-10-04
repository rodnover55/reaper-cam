#pragma once

// Камеры Windows: Media Foundation. Единственная библиотека, которая знает
// про ОС; наружу она отдаёт интерфейсы cam_capture.

#include "cam/capture/camera.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cam::capture_mf {

class MediaFoundationBackend : public capture::Backend {
public:
  /// Запускает Media Foundation; останавливает её деструктор.
  MediaFoundationBackend();
  ~MediaFoundationBackend() override;

  MediaFoundationBackend(const MediaFoundationBackend &) = delete;
  MediaFoundationBackend &operator=(const MediaFoundationBackend &) = delete;
  MediaFoundationBackend(MediaFoundationBackend &&) = delete;
  MediaFoundationBackend &operator=(MediaFoundationBackend &&) = delete;

  /// Камеры видеозахвата с их именами и режимами MJPEG. Признак камеры —
  /// символическая ссылка устройства: она переживает перезагрузку. Камера без
  /// MJPEG остаётся в списке с пустыми режимами — окно настроек скажет, что
  /// она не поддерживается.
  std::vector<capture::CameraInfo> list() override;

  /// Открывает камеру и читает её кадры MJPEG как есть: преобразователи
  /// форматов Media Foundation отключены.
  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const capture::CameraMode &mode) override;

  /// Не пусто, только если Media Foundation не запустилась.
  std::string unsupported() const override;

private:
  bool started_ = false;

  /// Режимы камер с прошлого перечня: камеру, которую держит идущий захват,
  /// система может не дать открыть второй раз ради списка режимов.
  std::mutex knownModesMutex_;
  std::map<std::string, std::vector<capture::CameraMode>> knownModes_;
};

} // namespace cam::capture_mf
