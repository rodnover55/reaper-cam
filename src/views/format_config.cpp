#include "views/format_config.hpp"

#include "format_choice.hpp"
#include "format_config.hpp"
#include "journal.hpp"
#include "services.hpp"
#include "views/ids.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace cam::reaper::views {
namespace {

/// Сообщение, которым окно REAPER забирает конфигурацию у окна настроек.
constexpr UINT kGetConfig = WM_USER + 1024;

// Данные окна: в SWELL GetWindowLong уже размером с указатель, в Win32 для
// этого есть варианты *Ptr.
#ifdef _WIN32
LONG_PTR windowData(HWND window) { return GetWindowLongPtr(window, GWLP_USERDATA); }
void setWindowData(HWND window, LONG_PTR value) {
  SetWindowLongPtr(window, GWLP_USERDATA, value);
}
#else
LONG_PTR windowData(HWND window) { return GetWindowLong(window, GWL_USERDATA); }
void setWindowData(HWND window, LONG_PTR value) { SetWindowLong(window, GWL_USERDATA, value); }
#endif

/// Выбор живёт с окном: создаётся при открытии, удаляется с окном.
FormatChoice *choiceOf(HWND dialog) {
  // NOLINTNEXTLINE(performance-no-int-to-ptr) — так хранятся данные окна
  return reinterpret_cast<FormatChoice *>(windowData(dialog));
}

WPARAM selectionOf(const std::optional<std::size_t> &index) {
  return index ? static_cast<WPARAM>(*index) : static_cast<WPARAM>(-1);
}

void showModes(HWND dialog, const FormatChoice &choice) {
  HWND modes = GetDlgItem(dialog, IDC_FORMAT_MODE);
  SendMessage(modes, CB_RESETCONTENT, 0, 0);
  if (choice.camera()) {
    for (const capture::CameraMode &mode : choice.cameras()[*choice.camera()].info.modes)
      SendMessage(modes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(modeLabel(mode).c_str()));
  }
  SendMessage(modes, CB_SETCURSEL, selectionOf(choice.mode()), 0);
  EnableWindow(modes, choice.camera() ? TRUE : FALSE);
}

void show(HWND dialog, const FormatChoice &choice) {
  SendDlgItemMessage(dialog, IDC_FORMAT_CAMERA, CB_SETCURSEL, selectionOf(choice.camera()), 0);
  showModes(dialog, choice);
  SetDlgItemText(dialog, IDC_FORMAT_STATUS, choice.status().c_str());
}

void onSelection(HWND dialog, int control) {
  FormatChoice *choice = choiceOf(dialog);
  const auto index = SendDlgItemMessage(dialog, control, CB_GETCURSEL, 0, 0);
  if (!choice || index < 0)
    return;

  if (control == IDC_FORMAT_CAMERA) {
    // Камера без MJPEG не выбирается: список возвращается к прежней, а строка
    // под ним объясняет почему.
    (void)choice->chooseCamera(static_cast<std::size_t>(index));
    show(dialog, *choice);
  } else if (control == IDC_FORMAT_MODE) {
    choice->chooseMode(static_cast<std::size_t>(index));
  }
}

std::vector<std::uint8_t> configBytes(const FormatChoice *choice) {
  const FormatConfig config = choice ? choice->config() : FormatConfig{};
  if (config.cameraId.empty()) // камера не выбрана — один код формата
    return {kFormatCodeBytes.begin(), kFormatCodeBytes.end()};
  return encodeConfig(config);
}

INT_PTR proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_INITDIALOG: {
    setWindowData(dialog, lParam);
    const FormatChoice &choice = *choiceOf(dialog);
    for (const FormatChoice::Camera &camera : choice.cameras())
      SendDlgItemMessage(dialog, IDC_FORMAT_CAMERA, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(cameraLabel(camera).c_str()));
    show(dialog, choice);
    return 1;
  }

  case WM_COMMAND:
    if (HIWORD(wParam) == CBN_SELCHANGE)
      onSelection(dialog, LOWORD(wParam));
    return 0;

  case kGetConfig: {
    const std::vector<std::uint8_t> bytes = configBytes(choiceOf(dialog));

    // Контракт окна REAPER: сначала спрашивает длину (`int*` в wParam), затем
    // даёт буфер этой длины (lParam).
    // NOLINTBEGIN(performance-no-int-to-ptr)
    if (wParam)
      *reinterpret_cast<int *>(wParam) = static_cast<int>(bytes.size());
    if (lParam)
      std::memcpy(reinterpret_cast<void *>(lParam), bytes.data(), bytes.size());
    // NOLINTEND(performance-no-int-to-ptr)
    journal("format config: get {} bytes (buffer {})", bytes.size(), lParam != 0);
    return 0;
  }

  case WM_DESTROY:
    journal("format config: window closed");
    delete choiceOf(dialog); // NOLINT(cppcoreguidelines-owning-memory) — владелец — окно
    setWindowData(dialog, 0);
    return 0;

  default:
    return 0;
  }
}

} // namespace

HWND showFormatConfig(const void *cfg, int cfgLength, HWND parent) {
  const std::span<const std::uint8_t> bytes =
      cfg && cfgLength > 0
          ? std::span<const std::uint8_t>(static_cast<const std::uint8_t *>(cfg),
                                          static_cast<std::size_t>(cfgLength))
          : std::span<const std::uint8_t>();
  const FormatConfig saved = decodeConfig(bytes).value_or(FormatConfig{});

  // Список камер — заново при каждом открытии окна (camera-capture).
  auto choice = std::make_unique<FormatChoice>(services().backend().list(), saved);
  journal("format config: {} cameras, saved \"{}\"", choice->cameras().size(), saved.cameraId);

  // Дальше выбором владеет окно: удаляет его на WM_DESTROY.
  const FormatChoice *owned = choice.release();
  HWND dialog = CreateDialogParam(nullptr, MAKEINTRESOURCE(IDD_FORMAT_CONFIG), parent, proc,
                                  reinterpret_cast<LPARAM>(owned));
  if (!dialog)
    delete owned; // окно не создалось — WM_DESTROY не придёт
  return dialog;
}

} // namespace cam::reaper::views
