#pragma once

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace cam::reaper {

/// Четырёхбайтный код формата. REAPER узнаёт по нему формат в конфигурации:
/// код стоит в её первых четырёх байтах (как у MP3 из SDK, `pcmsink_mp3lame.cpp`).
inline constexpr unsigned int kFormatCode = REAPER_FOURCC('r', 'c', 'a', 'm');

/// Регистрирует формат записи «видео с камеры» (design.md D1). Дорожка, которой
/// в Track recording settings назначен этот формат, становится дорожкой-камерой.
void registerFormat(reaper_plugin_info_t *rec);

/// Снимает регистрацию при выгрузке. Зовётся, когда `rec` уже недоступен,
/// поэтому указатель на `Register` сохраняется при загрузке.
void unregisterFormat();

} // namespace cam::reaper
