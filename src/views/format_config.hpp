#pragma once

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace cam::reaper::views {

/// Модуль, в ресурсах которого лежат формы, — сам модуль расширения. На
/// Windows без него форму искали бы в ресурсах REAPER; SWELL его не смотрит.
void setResourceModule(REAPER_PLUGIN_HINSTANCE module);

/// Окно настроек формата «видео с камеры», встроенное в окно REAPER (Track
/// recording settings, Render): камера и её режим MJPEG, больше ничего
/// (camera-capture). REAPER спрашивает у него конфигурацию сообщением
/// `WM_USER + 1024`: `wParam` — `int*` под длину, `lParam` — буфер под байты
/// (так же, как у MP3 из SDK, `pcmsink_mp3lame.cpp`).
HWND showFormatConfig(const void *cfg, int cfgLength, HWND parent);

} // namespace cam::reaper::views
