#pragma once

// Камеры Linux: V4L2 и метаданные UVC (design.md D6). Единственная
// библиотека, которая знает про ОС; наружу она отдаёт интерфейсы cam_capture.

#include "cam/capture/camera.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cam::capture_v4l2 {

/// Где лежат устойчивые ссылки на узлы камер. Параметр — ради тестов на
/// временном дереве каталогов.
inline constexpr std::string_view kDefaultLinkRoot = "/dev/v4l";

/// Узел устройства камеры по её устойчивому признаку: `by-id/<ссылка>` или
/// `by-path/<ссылка>` (design.md D2). Пусто, если ссылки нет — камера
/// отключена.
std::optional<std::filesystem::path>
resolveCamera(const std::string &id,
              const std::filesystem::path &linkRoot = std::filesystem::path(kDefaultLinkRoot));

class V4l2Backend : public capture::Backend {
public:
  explicit V4l2Backend(
      std::filesystem::path linkRoot = std::filesystem::path(kDefaultLinkRoot));

  /// Узлы захвата видео с их именами и режимами MJPEG. Узлы метаданных и
  /// режимы без MJPEG отбрасываются; камера без MJPEG остаётся в списке с
  /// пустыми режимами — окно настроек скажет, что она не поддерживается.
  std::vector<capture::CameraInfo> list() override;

  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const capture::CameraMode &mode) override;

private:
  std::filesystem::path linkRoot_;
};

} // namespace cam::capture_v4l2
