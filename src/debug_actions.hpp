#pragma once

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace cam::reaper {

/// Действия отладочной сборки для живых проверок (design.md D10). Их зовут
/// скрипты проверок по имени, например
/// `Main_OnCommand(NamedCommandLookup("_REAPER_CAM_DEBUG_DUMP_WINDOWS"), 0)`.
/// В обычной сборке не регистрируется ничего.
void registerDebugActions(reaper_plugin_info_t *rec);

void unregisterDebugActions();

} // namespace cam::reaper
