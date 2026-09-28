#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cam::capture {

/// Кадр тестового источника (design.md D10): на нём написаны время файла и
/// позиция шкалы, на которую кадр должен лечь по расчёту расширения. Глазом
/// их видно в окне видео, а проверки читают те же числа из полос клеток под
/// текстом — без распознавания букв.
///
/// Тестовый источник есть только в отладочной сборке и в автотестах:
/// пользователю он не показывается.
class TestPattern {
public:
  /// Кадры размером `width` × `height`, сжатые с качеством `quality` (1–100).
  TestPattern(int width, int height, int quality = 85);

  int width() const { return width_; }
  int height() const { return height_; }

  /// Рисует кадр и сжимает его в JPEG. Время и позиция — в секундах.
  std::vector<std::uint8_t> render(double fileTime, double timelinePosition) const;

private:
  int width_;
  int height_;
  int quality_;
};

/// Числа, прочитанные с кадра тестового источника, в миллисекундах.
struct TestPatternReading {
  std::int64_t fileTimeMs = 0;
  std::int64_t timelinePositionMs = 0;
};

/// Читает числа с разжатого кадра тестового источника. `rgb` — строки подряд,
/// по три байта на точку. Пусто, если на кадре нет полос тестового источника.
std::optional<TestPatternReading> readTestPattern(std::span<const std::uint8_t> rgb, int width,
                                                  int height);

} // namespace cam::capture
