#pragma once

// Прогрев камер по arm (design.md D3): поверхность управления без устройства
// получает от REAPER уведомления об arm и о смене списка дорожек и держит
// захваты камер у поставленных на запись дорожек-камер.

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace cam::reaper {

void registerArmWatch(reaper_plugin_info_t *rec);

/// Снимает поверхность и отпускает захваты — до остановки служб.
void unregisterArmWatch();

} // namespace cam::reaper
