#pragma once

// Общее для захвата всех ОС: режимы из того, что сообщает система, и время
// съёмки по метке системы в чужих часах. От ОС не зависит и проверяется
// тестами на любой.

#include "cam/capture/camera.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cam::capture {

/// Режим из размера и частоты дробью «кадров за столько секунд». Дробь
/// сокращается. Частота, отличающаяся от целой или от NTSC-частоты (N·1000/1001)
/// меньше чем на 0,01 %, становится ею: системы округляют длительность кадра
/// (333333 сотни наносекунд, 33333 микросекунды), и вместо 30/1 сообщают
/// 10000000/333333.
CameraMode modeOf(int width, int height, std::int64_t rateNumerator,
                  std::int64_t rateDenominator);

/// Частота `rateNumerator / rateDenominator` — частота режима с той же
/// точностью, с какой `modeOf` выравнивает частоты.
bool sameRate(const CameraMode &mode, std::int64_t rateNumerator,
              std::int64_t rateDenominator);

/// Режимы формата, частоту которого система задаёт диапазоном, — так у
/// встроенных камер Mac: «от 1 до 30 кадров». В список идут верхний край и
/// обычные частоты внутри диапазона (60, 50, 30, 25, 24, 20, 15, 10). Диапазон
/// из одной частоты — как у камер USB — даёт один режим. Частоты — дробью
/// «кадров за столько секунд», как у `modeOf`.
std::vector<CameraMode> modesInRateRange(int width, int height, std::int64_t lowNumerator,
                                         std::int64_t lowDenominator,
                                         std::int64_t highNumerator,
                                         std::int64_t highDenominator);

/// Частота режима внутри диапазона, края включены — с той же точностью, с
/// какой `modeOf` выравнивает частоты.
bool rateInRange(const CameraMode &mode, std::int64_t lowNumerator,
                 std::int64_t lowDenominator, std::int64_t highNumerator,
                 std::int64_t highDenominator);

/// Режимы по порядку списка в окне: крупные и частые — первыми, без повторов.
void sortModes(std::vector<CameraMode> &modes);

/// Время съёмки кадра в монотонных часах расширения по метке, которую система
/// поставила кадру в своих часах.
///
/// `arrival` — когда кадр пришёл, в часах расширения; `stampNow` — часы метки,
/// прочитанные тогда же; `stamp` — метка кадра. Время съёмки — приход минус
/// то, насколько метка старше «сейчас» её часов: так неважно, совпадают ли
/// часы метки с часами расширения.
///
/// Пусто, если метка позже прихода или старше `longest` секунд: такая метка
/// сбита, и честнее время прихода.
std::optional<double> captureTimeFromStamp(double arrival, double stampNow, double stamp,
                                           double longest = 0.5);

} // namespace cam::capture
