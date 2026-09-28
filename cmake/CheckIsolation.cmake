# Проверка изоляции библиотеки текстом (design.md D9).
#
# Направление зависимостей ловит попытку связаться с чужой библиотекой, но не
# ловит включения заголовков ОС и SDK REAPER: они лежат в системных путях и
# находятся без всякой зависимости. Не ловит оно и чужих понятий, просочившихся
# в имена и комментарии. И то и другое проверяется здесь.
#
# Механика одна на все библиотеки, списки — у каждой свои и лежат рядом с тем,
# что проверяют: `libs/<имя>/isolation.cmake`.

if(NOT DEFINED LIBRARY_DIR)
  message(FATAL_ERROR "LIBRARY_DIR не задан")
endif()

set(rules "${LIBRARY_DIR}/isolation.cmake")

if(NOT EXISTS "${rules}")
  message(FATAL_ERROR "нет списков изоляции: ${rules}")
endif()

# Списки приходят из подключаемого файла: `isolation_name` — как называть
# проверяемое в сообщении, остальные два — собственно правила.
set(isolation_name "")
set(forbidden_patterns "")
set(allowed_includes "")

include("${rules}")

if(isolation_name STREQUAL "")
  message(FATAL_ERROR "${rules} не задал isolation_name")
endif()

file(GLOB_RECURSE library_sources
  "${LIBRARY_DIR}/include/*.hpp"
  "${LIBRARY_DIR}/src/*.hpp"
  "${LIBRARY_DIR}/src/*.cpp")

if(library_sources STREQUAL "")
  message(FATAL_ERROR "в ${LIBRARY_DIR} не найдено исходников — проверка бессмысленна")
endif()

set(failures "")

foreach(source IN LISTS library_sources)
  file(READ "${source}" text)
  file(RELATIVE_PATH shown "${LIBRARY_DIR}" "${source}")

  foreach(pattern IN LISTS forbidden_patterns)
    string(REGEX MATCH "${pattern}" hit "${text}")
    if(NOT hit STREQUAL "")
      list(APPEND failures "${shown}: встретилось \"${hit}\"")
    endif()
  endforeach()

  string(REGEX MATCHALL "#include[ \t]*[<\"][^>\"]+[>\"]" includes "${text}")
  foreach(include IN LISTS includes)
    string(REGEX REPLACE "#include[ \t]*[<\"]([^>\"]+)[>\"]" "\\1" header "${include}")

    set(known NO)
    foreach(allowed IN LISTS allowed_includes)
      if(header MATCHES "^${allowed}")
        set(known YES)
      endif()
    endforeach()

    if(NOT known)
      list(APPEND failures "${shown}: посторонний заголовок <${header}>")
    endif()
  endforeach()
endforeach()

if(NOT failures STREQUAL "")
  list(JOIN failures "\n  " report)
  message(FATAL_ERROR "изоляция нарушена (${isolation_name}):\n  ${report}")
endif()

list(LENGTH library_sources count)
message(STATUS "изоляция цела (${isolation_name}): проверено файлов — ${count}")
