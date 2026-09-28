# Проверка форматирования (стиль в .clang-format).
#
# Список файлов собирается на запуске теста, а не на конфигурации: иначе новый
# файл остался бы непроверенным до следующего cmake.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED CLANG_FORMAT)
  message(FATAL_ERROR "SOURCE_DIR или CLANG_FORMAT не заданы")
endif()

file(GLOB_RECURSE sources
  "${SOURCE_DIR}/libs/*.cpp" "${SOURCE_DIR}/libs/*.hpp"
  "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.hpp"
  "${SOURCE_DIR}/tests/*.cpp" "${SOURCE_DIR}/tests/*.hpp")

if(sources STREQUAL "")
  message(FATAL_ERROR "исходников не найдено — проверка бессмысленна")
endif()

execute_process(
  COMMAND "${CLANG_FORMAT}" --dry-run -Werror ${sources}
  RESULT_VARIABLE status
  ERROR_VARIABLE report)

if(NOT status EQUAL 0)
  message(FATAL_ERROR
    "форматирование разошлось со стилем:\n${report}\n"
    "починить: cmake --build <каталог сборки> --target format")
endif()

list(LENGTH sources count)
message(STATUS "форматирование в порядке: проверено файлов — ${count}")
