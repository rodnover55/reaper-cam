// Точка входа расширения REAPER.
//
// REAPERAPI_IMPLEMENT создаёт определения указателей на функции API (ровно в
// одной единице трансляции), REAPERAPI_MINIMAL вместе с REAPERAPI_WANT_*
// ограничивает набор загружаемых функций теми, что действительно нужны.
#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_GetResourcePath
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayStateEx
#define REAPERAPI_WANT_GetPlayPosition
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_GetMediaTrackInfo_Value
#define REAPERAPI_WANT_SetMediaTrackInfo_Value
#define REAPERAPI_WANT_GetTrackStateChunk
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_projectconfig_var_getoffs

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include "arm_watch.hpp"
#include "build_stamp.hpp"
#include "debug_actions.hpp"
#include "format.hpp"
#include "journal.hpp"
#include "services.hpp"

#include <string>

extern "C" REAPER_PLUGIN_DLL_EXPORT int
REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec) {
  (void)instance;

  if (!rec) { // rec == nullptr — REAPER выгружает расширение
    cam::reaper::unregisterDebugActions();
    cam::reaper::unregisterArmWatch();
    cam::reaper::unregisterFormat();
    cam::reaper::unregisterMainTimer();
    cam::reaper::shutdownServices();
    cam::reaper::journal("unload");
    cam::reaper::closeJournal();
    return 0;
  }

  if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
    return 0;

  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0)
    return 0;

#ifdef CAM_DEBUG_BUILD
  // Журнал лежит в каталоге ресурсов: у каждого экземпляра REAPER он свой.
  cam::reaper::openJournal(std::string(GetResourcePath()) + "/reaper-cam.log");
  cam::reaper::journal("load (сборка {})", cam::reaper::kBuildStamp);
#endif

  cam::reaper::initServices();
  cam::reaper::registerMainTimer(rec);
  cam::reaper::registerFormat(rec);
  cam::reaper::registerArmWatch(rec);
  cam::reaper::registerDebugActions(rec);

  // Время сборки — чтобы по консоли было видно, тот ли модуль загружен:
  // забытый `cmake --install` выглядит как «ничего не изменилось».
  const std::string hello =
      std::string("reaper-cam loaded (сборка ") + cam::reaper::kBuildStamp + ")\n";

  ShowConsoleMsg(hello.c_str());

  return 1;
}
