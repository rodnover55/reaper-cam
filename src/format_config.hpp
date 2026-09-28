#pragma once

// Конфигурация формата «видео с камеры» (design.md D2): блок байтов, который
// REAPER хранит с дорожкой в <RECCFG> и передаёт в CreateSink и ShowConfig.
//
// Раскладка, все числа little-endian:
//
//   4  код формата — байты «macr», то есть REAPER_FOURCC('r','c','a','m')
//   4  версия раскладки
//   4  ширина кадра
//   4  высота кадра
//   4  частота кадров: числитель
//   4  частота кадров: знаменатель
//   2  длина признака камеры, затем его байты
//   2  длина имени камеры, затем его байты
//
// Конфигурация из одного кода — формат выбран, камера ещё нет. Так её пишет
// REAPER, пока окно настроек не отдало своей.
//
// Заголовков хоста здесь нет: разбор проверяется тестами без REAPER.

#include "cam/capture/camera.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cam::reaper {

/// Код формата в памяти — как его пишет REAPER_FOURCC('r', 'c', 'a', 'm').
inline constexpr std::array<std::uint8_t, 4> kFormatCodeBytes{'m', 'a', 'c', 'r'};

struct FormatConfig {
  static constexpr std::uint32_t kVersion = 1;

  /// Устойчивый признак камеры (camera-capture). Пусто — камера не выбрана.
  std::string cameraId;
  /// Имя камеры для сообщений: сама камера может быть отключена.
  std::string cameraName;
  capture::CameraMode mode;

  bool operator==(const FormatConfig &) const = default;
};

/// Наша ли это конфигурация: первые четыре байта — код формата.
bool isFormatConfig(std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> encodeConfig(const FormatConfig &config);

/// Разбор конфигурации. Пусто — не наша, оборвана или неизвестной версии:
/// тогда дорожка остаётся дорожкой-камерой без выбранной камеры.
std::optional<FormatConfig> decodeConfig(std::span<const std::uint8_t> bytes);

/// Байты формата записи из состояния дорожки (`GetTrackStateChunk`): блок
/// <RECCFG> хранит их в base64. Пусто — блока нет или он не разбирается.
std::optional<std::vector<std::uint8_t>> recordConfigOf(std::string_view trackChunk);

} // namespace cam::reaper
