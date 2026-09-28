#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_GetTrackStateChunk
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_projectconfig_var_getoffs

#include "debug_actions.hpp"

#include <reaper_plugin_functions.h>

#include "format_config.hpp"
#include "journal.hpp"
#include "test_source.hpp"
#include "views/format_config.hpp"
#include "views/ids.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cam::reaper {

#ifdef CAM_DEBUG_BUILD

namespace {

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

/// Шаги, отложенные на заданное число тиков таймера хоста (около 30 в
/// секунду). Модальное окно REAPER открывает вложенный цикл сообщений, и
/// скрипт проверки, открывший его, ждёт закрытия. Поэтому всё, что делается
/// с таким окном, делает сам таймер расширения: скрипт сначала зовёт
/// отладочное действие, а следом открывает окно.
///
/// Открывать окно должен скрипт, запущенный через `reaper -nonewinst`: из
/// скрипта на таймере (мост MCP, `defer`) и из `PostMessage` Track recording
/// settings не показывается вовсе, хотя точка отмены появляется.
std::vector<std::pair<int, std::function<void()>>> pendingSteps;

void schedule(int ticks, std::function<void()> step) {
  pendingSteps.emplace_back(ticks, std::move(step));
}

/// Наблюдатель за окном Track recording settings: ловится на первом же тике
/// таймера, когда появилось, и в нём выбираются пункты списков. Закрывает его
/// скрипт проверки — только тогда REAPER применяет выбор.
int watchRecordingSettingsTicks = 0;

/// Пункты, которые наблюдатель выбирает по порядку: формат, камера, режим.
std::vector<std::string> watchChoices{"Video (camera)"};

void watchRecordingSettings();

void onTimer() {
  if (watchRecordingSettingsTicks > 0) {
    --watchRecordingSettingsTicks;
    watchRecordingSettings();
  }

  std::vector<std::function<void()>> due;

  for (auto it = pendingSteps.begin(); it != pendingSteps.end();) {
    if (--it->first <= 0) {
      due.push_back(std::move(it->second));
      it = pendingSteps.erase(it);
    } else {
      ++it;
    }
  }

  for (auto &step : due)
    step();
}

std::string windowText(HWND hwnd) {
  std::array<char, 512> text{};
  GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
  return text.data();
}

std::string className(HWND hwnd) {
  std::array<char, 128> name{};
  GetClassName(hwnd, name.data(), static_cast<int>(name.size()));
  return name.data();
}

bool isCombo(HWND hwnd) {
  const std::string name = className(hwnd);
  return name.find("combo") != std::string::npos || name.find("Combo") != std::string::npos;
}

std::string comboItem(HWND combo, int index) {
  std::array<char, 512> item{};
  SendMessage(combo, CB_GETLBTEXT, static_cast<WPARAM>(index),
              reinterpret_cast<LPARAM>(item.data()));
  return item.data();
}

/// Пункты выпадающего списка: по ним видно, какие форматы записи REAPER
/// предлагает в своих окнах.
void journalComboItems(HWND combo) {
  const auto count = static_cast<int>(SendMessage(combo, CB_GETCOUNT, 0, 0));
  const auto selected = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));

  for (int i = 0; i < count; ++i)
    journal("      item {}{}: {}", i, i == selected ? " (selected)" : "", comboItem(combo, i));
}

int controlId(HWND hwnd) { return static_cast<int>(GetWindowLong(hwnd, GWL_ID)); }

BOOL journalChild(HWND child, LPARAM /*unused*/) {
  journal("    child id={} class={} visible={} text={}", controlId(child), className(child),
          IsWindowVisible(child) != 0, windowText(child));

  if (isCombo(child))
    journalComboItems(child);

  return TRUE;
}

BOOL journalTopWindow(HWND hwnd, LPARAM /*unused*/) {
  journal("  window {} class={} visible={} title={}", static_cast<void *>(hwnd),
          className(hwnd), IsWindowVisible(hwnd) != 0, windowText(hwnd));

  EnumChildWindows(hwnd, journalChild, 0);
  return TRUE;
}

void dumpWindows() {
  journal("dump windows");
  EnumWindows(journalTopWindow, 0);
  journal("dump windows done");
}

/// Окно верхнего уровня, заголовок которого начинается с `prefix`.
HWND findTopWindow(const std::string &prefix) {
  struct Search {
    std::string prefix;
    HWND found = nullptr;
  } search{prefix};

  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        auto *s = reinterpret_cast<Search *>(lp);
        if (windowText(hwnd).starts_with(s->prefix)) {
          s->found = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));

  return search.found;
}

/// Выбирает в окне пункт выпадающего списка, как если бы это сделал
/// пользователь: выделение плюс уведомление окну о смене выбора.
bool chooseComboItem(HWND dialog, const std::string &wanted) {
  struct Search {
    std::string wanted;
    HWND combo = nullptr;
    int index = -1;
  } search{wanted};

  EnumChildWindows(
      dialog,
      [](HWND child, LPARAM lp) -> BOOL {
        auto *s = reinterpret_cast<Search *>(lp);
        if (!isCombo(child))
          return TRUE;

        const auto count = static_cast<int>(SendMessage(child, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
          if (comboItem(child, i) == s->wanted) {
            s->combo = child;
            s->index = i;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));

  if (!search.combo)
    return false;

  // Уведомление получает родитель списка: у встроенного окна формата это оно
  // само, а не окно REAPER, в котором оно лежит.
  const int id = controlId(search.combo);
  SendMessage(search.combo, CB_SETCURSEL, static_cast<WPARAM>(search.index), 0);
  SendMessage(GetParent(search.combo), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE),
              reinterpret_cast<LPARAM>(search.combo));
  return true;
}

constexpr int kSecond = 30; // тиков таймера хоста

const char *const kRecordingSettingsTitle = "Track Recording Settings";

/// Блок <RECCFG> первой дорожки — чтобы видеть, на каком шаге окно
/// записывает формат.
void journalRecCfg(const char *step) {
  std::string chunk(65536, '\0');
  MediaTrack *track = GetTrack(nullptr, 0);
  if (!track ||
      !GetTrackStateChunk(track, chunk.data(), static_cast<int>(chunk.size()), false)) {
    journal("reccfg {}: no track", step);
    return;
  }

  const auto begin = chunk.find("<RECCFG");
  if (begin == std::string::npos) {
    journal("reccfg {}: none", step);
    return;
  }

  std::string block = chunk.substr(begin, chunk.find('>', begin) - begin);
  for (char &c : block)
    if (c == '\n')
      c = ' ';
  journal("reccfg {}: {}", step, block);
}

void watchRecordingSettings() {
  HWND dialog = findTopWindow(kRecordingSettingsTitle);
  if (!dialog) {
    if (watchRecordingSettingsTicks == 0)
      journal("watch recording settings: gave up");
    return;
  }

  watchRecordingSettingsTicks = 0;
  journal("watch recording settings: found {}", static_cast<void *>(dialog));

  // Галочка «Set recording audio format»: без неё дорожка пишет формат
  // проекта, что бы ни было выбрано в списке.
  constexpr int kSetFormatCheckbox = 1044;
  HWND checkbox = GetDlgItem(dialog, kSetFormatCheckbox);
  const auto checked = SendMessage(checkbox, BM_GETCHECK, 0, 0);
  journal("watch recording settings: set-format checkbox={}", checked);
  journalRecCfg("before");
  if (checked != BST_CHECKED) {
    SendMessage(checkbox, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(dialog, WM_COMMAND, MAKEWPARAM(kSetFormatCheckbox, BN_CLICKED),
                reinterpret_cast<LPARAM>(checkbox));
  }
  journal("watch recording settings: checkbox now={}",
          SendMessage(checkbox, BM_GETCHECK, 0, 0));
  journalRecCfg("after checkbox");

  for (const std::string &choice : watchChoices)
    journal("watch recording settings: choose \"{}\" -> {}", choice,
            chooseComboItem(dialog, choice));
  journalRecCfg("after select");
  dumpWindows();

  // Выбор REAPER применяет, когда окно закрывается по-настоящему: фокус ушёл к
  // другому окну или нажат крестик. Тогда он и забирает конфигурацию у окна
  // формата (WM_USER + 1024). Поддельные WM_ACTIVATE, WM_CLOSE и IDOK окно не
  // закрывают — скрипт проверки закрывает его крестиком через X11.
  journal("watch recording settings: chosen, waiting for the window to close");
}

/// Текст контрола окна.
std::string itemText(HWND dialog, int id) {
  std::array<char, 512> text{};
  GetDlgItemText(dialog, id, text.data(), static_cast<int>(text.size()));
  return text.data();
}

/// Что окно настроек формата показывает и какую конфигурацию отдаст REAPER.
void journalFormatDialog(HWND dialog, const std::string &step) {
  journal("format dialog {}: status \"{}\"", step, itemText(dialog, IDC_FORMAT_STATUS));
  journal("    cameras:");
  journalComboItems(GetDlgItem(dialog, IDC_FORMAT_CAMERA));
  journal("    modes (enabled={}):",
          IsWindowEnabled(GetDlgItem(dialog, IDC_FORMAT_MODE)) != 0);
  journalComboItems(GetDlgItem(dialog, IDC_FORMAT_MODE));

  // Как REAPER: сначала длина, затем байты.
  constexpr UINT kGetConfig = WM_USER + 1024;
  int size = 0;
  SendMessage(dialog, kGetConfig, reinterpret_cast<WPARAM>(&size), 0);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max(size, 0)));
  SendMessage(dialog, kGetConfig, 0, reinterpret_cast<LPARAM>(bytes.data()));

  const auto config = decodeConfig(bytes);
  if (!config) {
    journal("    config: {} bytes, not ours", bytes.size());
    return;
  }
  journal("    config: {} bytes, camera \"{}\" ({}) {}x{} {}/{}", bytes.size(),
          config->cameraName, config->cameraId, config->mode.width, config->mode.height,
          config->mode.rateNumerator, config->mode.rateDenominator);
}

/// Окно настроек формата без окна REAPER: скрытое дочернее окно главного, с
/// конфигурацией первой дорожки. По очереди выбирается каждая камера; фокус
/// и видимые окна не меняются.
void exerciseFormatDialog() {
  std::vector<std::uint8_t> cfg;
  if (MediaTrack *track = GetTrack(nullptr, 0)) {
    std::string chunk(65536, '\0');
    if (GetTrackStateChunk(track, chunk.data(), static_cast<int>(chunk.size()), false)) {
      chunk.resize(chunk.find('\0'));
      cfg = recordConfigOf(chunk).value_or(std::vector<std::uint8_t>{});
    }
  }

  HWND dialog =
      views::showFormatConfig(cfg.data(), static_cast<int>(cfg.size()), GetMainHwnd());
  journal("format dialog: window {} for track config of {} bytes", static_cast<void *>(dialog),
          cfg.size());
  if (!dialog)
    return;

  journalFormatDialog(dialog, "opened");
  HWND cameras = GetDlgItem(dialog, IDC_FORMAT_CAMERA);
  const auto count = static_cast<int>(SendMessage(cameras, CB_GETCOUNT, 0, 0));
  for (int index = 0; index < count; ++index) {
    SendMessage(cameras, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    SendMessage(dialog, WM_COMMAND, MAKEWPARAM(IDC_FORMAT_CAMERA, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(cameras));
    journalFormatDialog(dialog, "after choosing camera " + std::to_string(index));
  }
  DestroyWindow(dialog);
}

struct DebugAction {
  custom_action_register_t registration;
  std::function<void()> run;
  int command = 0;
};

std::array<DebugAction, 5> actions{{
    {{0, "REAPER_CAM_DEBUG_DUMP_WINDOWS", "reaper-cam (debug): journal open windows", nullptr},
     [] { schedule(kSecond, dumpWindows); }},
    {{0, "REAPER_CAM_DEBUG_DUMP_RECORDING_SETTINGS",
      "reaper-cam (debug): journal track recording settings and close them", nullptr},
     [] {
       schedule(kSecond, [] {
         dumpWindows();
         if (HWND dialog = findTopWindow(kRecordingSettingsTitle))
           SendMessage(dialog, WM_COMMAND, IDCANCEL, 0);
       });
     }},
    {{0, "REAPER_CAM_DEBUG_CHOOSE_CAMERA_FORMAT",
      "reaper-cam (debug): choose Video (camera) in track recording settings", nullptr},
     [] {
       schedule(kSecond, [] {
         HWND dialog = findTopWindow(kRecordingSettingsTitle);
         journal("choose format: dialog={}", static_cast<void *>(dialog));
         if (!dialog)
           return;

         journal("choose format: selected={}", chooseComboItem(dialog, "Video (camera)"));
         schedule(kSecond, [dialog] {
           dumpWindows();
           SendMessage(dialog, WM_COMMAND, IDOK, 0);
           journal("choose format: OK pressed");
         });
       });
     }},
    {{0, "REAPER_CAM_DEBUG_WATCH_RECORDING_SETTINGS",
      "reaper-cam (debug): catch track recording settings and choose Video (camera)", nullptr},
     [] { watchRecordingSettingsTicks = 10 * kSecond; }},
    {{0, "REAPER_CAM_DEBUG_FORMAT_DIALOG",
      "reaper-cam (debug): exercise the camera format window hidden, journal its lists",
      nullptr},
     [] { exerciseFormatDialog(); }},
}};

/// Переменная настроек REAPER по имени: сначала проектная, потом глобальная.
/// Скрипты проверок ставят ею то, для чего нет действий, например count-in
/// (биты `projmetroen`). Возвращает прежнее значение, NaN — если имени нет.
double configVar(const char *name, double value, bool set) {
  if (!name)
    return std::numeric_limits<double>::quiet_NaN();

  int size = 0;
  void *address = nullptr;

  const int offset = projectconfig_var_getoffs(name, &size);
  if (size > 0)
    address = projectconfig_var_addr(EnumProjects(-1, nullptr, 0), offset);

  if (!address) {
    size = 0;
    address = get_config_var(name, &size);
  }

  if (!address || size <= 0)
    return std::numeric_limits<double>::quiet_NaN();

  double previous = std::numeric_limits<double>::quiet_NaN();

  if (size == sizeof(double)) {
    std::memcpy(&previous, address, sizeof(double));
    if (set)
      std::memcpy(address, &value, sizeof(double));
  } else if (size == sizeof(std::int32_t)) {
    std::int32_t current = 0;
    std::memcpy(&current, address, sizeof(current));
    previous = current;
    if (set) {
      const auto next = static_cast<std::int32_t>(std::lround(value));
      std::memcpy(address, &next, sizeof(next));
    }
  } else if (size == 1) {
    std::uint8_t current = 0;
    std::memcpy(&current, address, 1);
    previous = current;
    if (set) {
      const auto next = static_cast<std::uint8_t>(std::lround(value));
      std::memcpy(address, &next, 1);
    }
  }

  journal("config var {} size={} {} -> {}", name, size, previous, set ? value : previous);
  return previous;
}

/// Обёртка для ReaScript: числа приходят указателями на double, логическое —
/// целым в указателе (reaper_plugin.h, APIvararg_*).
void *configVarVararg(void **args, int count) {
  thread_local double result = 0.0;
  if (count < 3)
    return &result;

  const auto *name = static_cast<const char *>(args[0]);
  const double value = args[1] ? *static_cast<const double *>(args[1]) : 0.0;
  const bool set = reinterpret_cast<std::intptr_t>(args[2]) != 0;
  result = configVar(name, value, set);
  return &result;
}

/// Пункты для наблюдателя за Track recording settings, через «;», и его
/// запуск: скрипт зовёт это перед тем, как открыть окно.
void watchRecordingSettingsFor(const char *choices) {
  watchChoices.clear();
  std::string rest = choices ? choices : "";
  while (!rest.empty()) {
    const auto end = rest.find(';');
    watchChoices.push_back(rest.substr(0, end));
    rest = end == std::string::npos ? "" : rest.substr(end + 1);
  }
  watchRecordingSettingsTicks = 10 * kSecond;
  journal("watch recording settings for {} choices", watchChoices.size());
}

void *watchRecordingSettingsForVararg(void **args, int count) {
  if (count >= 1)
    watchRecordingSettingsFor(static_cast<const char *>(args[0]));
  return nullptr;
}

const char *const kWatchDef =
    "void\0const char*\0choices\0"
    "reaper-cam (debug): catch track recording settings, choose list items (;-separated)";

/// Отказ тестовой камеры по команде скрипта: потеря камеры посреди дубля.
void *failTestCameraVararg(void **args, int count) {
  if (count >= 1)
    failTestCamera(reinterpret_cast<std::intptr_t>(args[0]) != 0);
  return nullptr;
}

const char *const kFailDef =
    "void\0bool\0failing\0"
    "reaper-cam (debug): make the test camera fail like an unplugged one";

/// Строка от скрипта проверки в журнал: так в журнале видно, где какой
/// сценарий.
void journalFromScript(const char *text) { journal("script: {}", text ? text : ""); }

void *journalFromScriptVararg(void **args, int count) {
  if (count >= 1)
    journalFromScript(static_cast<const char *>(args[0]));
  return nullptr;
}

const char *const kJournalDef = "void\0const char*\0text\0"
                                "reaper-cam (debug): write a line into reaper-cam.log";

const char *const kConfigVarDef =
    "double\0const char*,double,bool\0name,value,set\0"
    "reaper-cam (debug): read and optionally set a REAPER config "
    "variable, returns the previous value";

bool onAction(KbdSectionInfo * /*section*/, int command, int /*val*/, int /*val2*/,
              int /*relmode*/, HWND /*hwnd*/) {
  if (command == 0)
    return false;

  for (auto &action : actions) {
    if (action.command == command) {
      journal("debug action {}", action.registration.idStr);
      action.run();
      return true;
    }
  }

  return false;
}

} // namespace

void registerDebugActions(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;

  for (auto &action : actions) {
    action.command = hostRegister("custom_action", &action.registration);
    journal("debug action {} -> {}", action.registration.idStr, action.command);
  }

  hostRegister("hookcommand2", reinterpret_cast<void *>(onAction));
  hostRegister("timer", reinterpret_cast<void *>(onTimer));

  hostRegister("API_CamDebug_ConfigVar", reinterpret_cast<void *>(configVar));
  hostRegister("APIvararg_CamDebug_ConfigVar", reinterpret_cast<void *>(configVarVararg));
  hostRegister("APIdef_CamDebug_ConfigVar", const_cast<char *>(kConfigVarDef));

  hostRegister("API_CamDebug_WatchRecordingSettings",
               reinterpret_cast<void *>(watchRecordingSettingsFor));
  hostRegister("APIvararg_CamDebug_WatchRecordingSettings",
               reinterpret_cast<void *>(watchRecordingSettingsForVararg));
  hostRegister("APIdef_CamDebug_WatchRecordingSettings", const_cast<char *>(kWatchDef));

  hostRegister("API_CamDebug_FailTestCamera", reinterpret_cast<void *>(failTestCamera));
  hostRegister("APIvararg_CamDebug_FailTestCamera",
               reinterpret_cast<void *>(failTestCameraVararg));
  hostRegister("APIdef_CamDebug_FailTestCamera", const_cast<char *>(kFailDef));

  hostRegister("API_CamDebug_Journal", reinterpret_cast<void *>(journalFromScript));
  hostRegister("APIvararg_CamDebug_Journal",
               reinterpret_cast<void *>(journalFromScriptVararg));
  hostRegister("APIdef_CamDebug_Journal", const_cast<char *>(kJournalDef));
}

void unregisterDebugActions() {
  if (!hostRegister)
    return;

  hostRegister("-API_CamDebug_ConfigVar", reinterpret_cast<void *>(configVar));
  hostRegister("-API_CamDebug_Journal", reinterpret_cast<void *>(journalFromScript));
  hostRegister("-API_CamDebug_FailTestCamera", reinterpret_cast<void *>(failTestCamera));
  hostRegister("-API_CamDebug_WatchRecordingSettings",
               reinterpret_cast<void *>(watchRecordingSettingsFor));
  hostRegister("-timer", reinterpret_cast<void *>(onTimer));
  hostRegister("-hookcommand2", reinterpret_cast<void *>(onAction));

  for (auto &action : actions)
    hostRegister("-custom_action", &action.registration);
}

#else

void registerDebugActions(reaper_plugin_info_t * /*rec*/) {}

void unregisterDebugActions() {}

#endif

} // namespace cam::reaper
