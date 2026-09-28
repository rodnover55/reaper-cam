#pragma once

// Время съёмки кадра из метаданных UVC без прав администратора (design.md D6).
//
// Камера UVC пишет в заголовок каждого пакета данных отсчёты своих часов
// (STC): PTS — когда началась съёмка кадра, и SCR — значение часов около
// момента передачи пакета. Драйвер, отдавая заголовки через узел метаданных,
// приписывает к каждому момент, когда хост его обработал, по монотонным часам.
//
// Связь «часы камеры → монотонное время» строится прямо по парам «SCR —
// момент обработки»: пакет не может быть обработан раньше, чем передан,
// поэтому истинная связь — нижняя огибающая этих точек, а всё, что выше, —
// задержка обработки. Номера кадров шины из SCR для связи не годятся: камера
// ноутбука нумерует их своим счётчиком со случайным от сеанса к сеансу
// сдвигом относительно номера на шине (findings.md — время съёмки). На этом
// же спотыкается и связь, которую строит сам драйвер при hwtimestamps=1.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace cam::capture_v4l2 {

/// Заголовок одного пакета из узла метаданных (`V4L2_META_FMT_UVC`).
struct UvcSample {
  /// Когда хост обработал пакет: наносекунды монотонных часов.
  std::uint64_t hostNs = 0;
  /// Номер кадра шины USB по хосту в этот момент (11 бит).
  std::uint16_t hostSof = 0;
  /// Часы камеры в начале съёмки кадра.
  std::optional<std::uint32_t> pts;
  /// Часы камеры около передачи пакета (SCR).
  std::optional<std::uint32_t> stc;
  /// Номер кадра шины по камере из SCR (11 бит).
  std::uint16_t deviceSof = 0;
};

/// Заголовки из одного буфера узла метаданных. Буфер — записи подряд:
/// `struct uvc_meta_buf` и за ней остаток заголовка пакета.
std::vector<UvcSample> parseUvcMetadata(std::span<const std::uint8_t> buffer);

/// Связь часов камеры с монотонными часами хоста.
class UvcClock {
public:
  struct Settings {
    /// Сколько секунд последних отсчётов входит в связь.
    double window = 5.0;
    /// Меньше отсчётов — связи ещё нет.
    std::size_t minimumSamples = 64;
    /// Доля нижних точек, по которым проводится огибающая, и процентиль,
    /// которым она опускается на дно.
    double lowerShare = 0.10;
    double bottomPercentile = 0.02;
    /// Разброс задержки обработки шире этого — связь неустойчива.
    double widestSpreadSeconds = 0.020;
  };

  UvcClock();
  explicit UvcClock(Settings settings);

  /// Отсчёт с SCR. Отсчёты без SCR связи не несут и пропускаются.
  void add(const UvcSample &sample);

  /// Начало съёмки кадра с данным PTS в секундах монотонных часов. Пусто,
  /// если связи ещё нет или она неустойчива: тогда кадру остаётся метка
  /// буфера.
  std::optional<double> exposureStart(std::uint32_t pts) const;

  /// Частота часов камеры по наклону связи, Гц. Пусто, если связи нет.
  std::optional<double> deviceClockHz() const;

private:
  /// Отсчёт на непрерывной шкале часов камеры: переполнения 32 бит сняты.
  struct Point {
    double ticks;
    double hostNs;
  };

  /// hostNs = originNs + (ticks − originTicks) · nsPerTick
  struct Line {
    double originTicks = 0.0;
    double originNs = 0.0;
    double nsPerTick = 0.0;
  };

  std::optional<Line> fit() const;

  Settings settings_;
  std::deque<Point> points_;

  std::optional<std::uint32_t> lastStc_;
  double ticks_ = 0.0;

  mutable std::optional<Line> cached_;
  mutable double cachedAtNs_ = 0.0;
};

} // namespace cam::capture_v4l2
