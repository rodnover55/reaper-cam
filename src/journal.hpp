#pragma once

// Журнал отладочной сборки: что и из какого потока REAPER зовёт у расширения
// (design.md D10). В обычной сборке вызовы журнала пусты и ничего не стоят.

#include <chrono>
#include <format>
#include <string>
#include <string_view>
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

template <class... Args> void journal(std::format_string<Args...> format, Args &&...args) {
#ifdef CAM_DEBUG_BUILD
  journalLine(std::format(format, std::forward<Args>(args)...));
#else
  (void)format;
  ((void)args, ...);
#endif
}

} // namespace cam::reaper
