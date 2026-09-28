# Отметка времени сборки для приветственной строки расширения.
#
# Существует ради живых проверок: по строке в консоли REAPER видно, тот ли
# модуль загружен. Вчерашний плагин на месте свежего выглядит как «поведение не
# изменилось», и отличить это от настоящей ошибки иначе нечем.
#
# Пишется отдельным шагом на каждую сборку, а не `string(TIMESTAMP)` при
# конфигурации: та отметка замерла бы на времени `cmake -B`.

if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT не задан")
endif()

string(TIMESTAMP stamp "%Y-%m-%d %H:%M:%S")

file(WRITE "${OUTPUT}"
"#pragma once

// Создаётся при каждой сборке — см. cmake/WriteBuildStamp.cmake.

namespace cam::reaper {

inline constexpr const char *kBuildStamp = \"${stamp}\";

} // namespace cam::reaper
")
