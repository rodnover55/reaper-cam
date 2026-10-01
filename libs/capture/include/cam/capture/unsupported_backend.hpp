#pragma once

#include "cam/capture/camera.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cam::capture {

/// Пустышка на месте камер ОС, для которой захвата ещё нет (design.md D9):
/// камер в ней нет, открыть ничего нельзя. Модуль собирается и работает и
/// так — дорожка-камера пишет чёрный дубль, а окно настроек и консоль
/// говорят почему.
class UnsupportedBackend : public Backend {
public:
  /// `system` — имя ОС для сообщений: «Windows», «macOS».
  explicit UnsupportedBackend(std::string system);

  std::vector<CameraInfo> list() override { return {}; }

  /// Всегда ошибка устройства. Причина короткая, как у прочих («busy»,
  /// «not connected»): консоль покажет её в скобках у дорожки, которая
  /// помнит камеру с другой ОС.
  std::unique_ptr<Capture> open(const std::string &id, const CameraMode &mode) override;

  std::string unsupported() const override;

private:
  std::string system_;
};

} // namespace cam::capture
