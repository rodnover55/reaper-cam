#pragma once

// Тестовый источник кадров как камера (design.md D10): камера «test/pattern»
// рисует на кадре позицию шкалы, которая звучала в момент съёмки. Если
// расширение ставит кадры верно, в окне видео нарисованная позиция совпадает
// с курсором. Есть только в отладочной сборке: пользователю не показывается.

#include "cam/capture/camera.hpp"

#include <memory>

namespace cam::reaper {

/// Признак тестовой камеры в конфигурации формата.
inline constexpr const char *kTestCameraId = "test/pattern";

/// В отладочной сборке — `inner` с тестовой камерой в придачу, в обычной —
/// сам `inner`.
std::unique_ptr<capture::Backend> withTestSource(std::unique_ptr<capture::Backend> inner);

/// Отказ тестовой камеры: пока включён, её захват бросает ошибку устройства,
/// как отключённая камера. В обычной сборке ничего не делает.
void failTestCamera(bool failing);

} // namespace cam::reaper
