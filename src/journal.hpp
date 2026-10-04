#pragma once

// Журнал отладочной сборки: что и из какого потока REAPER зовёт у расширения
// (design.md D10). В обычной сборке вызовы журнала пусты и ничего не стоят.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cam::reaper {

/// Открывает журнал в файле `path`, дописывая в конец. Зовётся при загрузке
/// расширения из главного потока; этот поток в журнале называется `main`.
void openJournal(const std::string &path);

void closeJournal();

/// Пишет строку с отметкой времени и потоком. Можно звать из любого потока,
/// в том числе звукового: журнал есть только в отладочной сборке, и задержка
/// на записи в файл там допустима.
void journalLine(std::string_view line);

/// Время по часам журнала, в секундах от его открытия: для отметок, снятых
/// в одном потоке, а записанных в журнал из другого.
double journalSeconds(std::chrono::steady_clock::time_point at);

namespace detail {

/// Дробное число в строке журнала. std::format с double в libc++ системы есть
/// только с macOS 13.3, а модуль грузится и на более старых; поэтому дробные
/// аргументы журнала печатаются через snprintf (std::formatter ниже).
struct JournalReal {
  double value;
};

template <class T>
using JournalArg =
    std::conditional_t<std::is_floating_point_v<std::remove_cvref_t<T>>, JournalReal, T>;

template <class T> decltype(auto) journalArg(T &&value) {
  if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>)
    return JournalReal{static_cast<double>(value)};
  else
    return std::forward<T>(value);
}

} // namespace detail

template <class... Args>
void journal(std::format_string<detail::JournalArg<Args>...> format, Args &&...args) {
#ifdef CAM_DEBUG_BUILD
  journalLine(std::format(format, detail::journalArg(std::forward<Args>(args))...));
#else
  (void)format;
  ((void)args, ...);
#endif
}

} // namespace cam::reaper

/// Печатает JournalReal: `{:.6f}` — шесть знаков после точки, `{}` — как %g.
/// Других спецификаций журнал не пишет.
template <> struct std::formatter<cam::reaper::detail::JournalReal> {
  int precision = -1;

  constexpr auto parse(std::format_parse_context &context) {
    const auto *it = context.begin();
    if (it != context.end() && *it == '.') {
      precision = 0;
      for (++it; it != context.end() && *it >= '0' && *it <= '9'; ++it)
        precision = (precision * 10) + (*it - '0');
      if (it != context.end() && *it == 'f')
        ++it;
    }
    if (it != context.end() && *it != '}')
      throw std::format_error("journal: only {} and {:.Nf} are supported for numbers");
    return it;
  }

  auto format(const cam::reaper::detail::JournalReal &real,
              std::format_context &context) const {
    std::array<char, 64> text{};
    const int written =
        precision >= 0 ? std::snprintf(text.data(), text.size(), "%.*f", precision, real.value)
                       : std::snprintf(text.data(), text.size(), "%g", real.value);
    const auto length =
        written > 0 ? std::min(static_cast<std::size_t>(written), text.size() - 1) : 0;
    return std::copy_n(text.data(), length, context.out());
  }
};
