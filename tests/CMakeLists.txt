# Один исполняемый файл на все библиотеки: тесты быстрые, REAPER и камера им
# не нужны, а дробить их по целям нечем — граница библиотек проверена
# направлением зависимостей и изоляцией.
add_executable(cam_tests
  main.cpp
  jpeg_decode.cpp
  run_command.cpp
  capture_device_registry_test.cpp
  capture_test_pattern_test.cpp
  container_jpeg_tables_test.cpp
  container_writer_test.cpp
  format_choice_test.cpp
  format_config_test.cpp
  take_test.cpp
  ${CMAKE_SOURCE_DIR}/src/format_choice.cpp
  ${CMAKE_SOURCE_DIR}/src/format_config.cpp
  ${CMAKE_SOURCE_DIR}/src/take.cpp
  timing_clock_bridge_test.cpp
  timing_frame_grid_test.cpp
  timing_spsc_ring_test.cpp)

# wdl_lice — чтобы разжимать кадры тем же кодом WDL, каким их читает хост.
target_link_libraries(cam_tests PRIVATE
  doctest::doctest
  cam_capture
  cam_container
  cam_timing
  wdl_lice)

target_include_directories(cam_tests PRIVATE .)

# Разбор конфигурации формата — единица модуля, которой хост не нужен: она
# проверяется здесь без REAPER.
target_include_directories(cam_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

# Захват Linux проверяется только там, где он собирается (design.md D9).
if(TARGET cam_capture_v4l2)
  target_sources(cam_tests PRIVATE
    capture_v4l2_resolve_test.cpp
    capture_v4l2_uvc_clock_test.cpp)
  target_link_libraries(cam_tests PRIVATE cam_capture_v4l2)
endif()

cam_enable_warnings(cam_tests)

# doctest.h включает <ciso646>, а libstdc++ 15 отвечает на него директивой
# #warning. Пометка заголовков системными тут не спасает: clang показывает
# #warning и из системных заголовков, а с -Werror это останавливает сборку.
# Гасим на одной цели — только здесь и подключается doctest.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(cam_tests PRIVATE "-Wno-#warnings")
endif()

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
doctest_discover_tests(cam_tests)

# Изоляция проверяется отдельно и текстом: заголовки ОС и SDK REAPER находятся
# в системных путях без всякой зависимости, и направление зависимостей их не
# ловит (design.md D9). Механика общая, списки — у каждой библиотеки свои и
# лежат в её isolation.cmake.
foreach(isolated timing container capture)
  add_test(NAME ${isolated}_isolation
    COMMAND "${CMAKE_COMMAND}"
      -D "LIBRARY_DIR=${CMAKE_SOURCE_DIR}/libs/${isolated}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CheckIsolation.cmake")

  set_tests_properties(${isolated}_isolation PROPERTIES LABELS "lint")
endforeach()

# --- Проверки исходников ------------------------------------------------------
# Линтеры оформлены тестами CTest, а не отдельными целями: так «прогнать всё»
# остаётся одной командой, а метки позволяют отделить быстрое от долгого.
find_program(CAM_CLANG_FORMAT NAMES clang-format)
find_program(CAM_RUN_CLANG_TIDY NAMES run-clang-tidy)

if(CAM_CLANG_FORMAT)
  add_test(NAME clang_format
    COMMAND "${CMAKE_COMMAND}"
      -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
      -D "CLANG_FORMAT=${CAM_CLANG_FORMAT}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CheckFormat.cmake")

  set_tests_properties(clang_format PROPERTIES LABELS "lint")

  file(GLOB_RECURSE cam_formattable
    "${CMAKE_SOURCE_DIR}/libs/*.cpp" "${CMAKE_SOURCE_DIR}/libs/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp" "${CMAKE_SOURCE_DIR}/tests/*.hpp")

  add_custom_target(format
    COMMAND "${CAM_CLANG_FORMAT}" -i ${cam_formattable}
    COMMENT "Форматирование исходников по .clang-format")
else()
  message(STATUS "clang-format не найден — проверка форматирования пропущена")
endif()

# clang-tidy дорог: он разбирает и проверяет каждый файл заново вместе со всеми
# заголовками и сам между запусками ничего не помнит. Поэтому он идёт через
# ctcache — кеш результатов по файлу. Ключ — вывод препроцессора, строки
# #include файла и конфигурация clang-tidy; сохраняется только чистый прогон.
# Неизменённый файл не анализируется вовсе, файл с замечаниями — каждый раз
# заново, так что замечание кеш не прячет.
#
# Метка slow остаётся: первый прогон и правка общего заголовка по-прежнему
# занимают минуты. Повседневный прогон — `ctest --preset quick`, полный —
# push-check.
if(CAM_RUN_CLANG_TIDY AND CMAKE_EXPORT_COMPILE_COMMANDS)
  include(ProcessorCount)
  ProcessorCount(cam_cpu_count)
  if(cam_cpu_count EQUAL 0)
    set(cam_cpu_count 1)
  endif()

  find_program(CAM_CLANG_TIDY NAMES clang-tidy)
  find_package(Python3 COMPONENTS Interpreter)

  set(cam_clang_tidy_binary clang-tidy)
  set(cam_clang_tidy_environment "")

  if(CAM_CLANG_TIDY AND Python3_Interpreter_FOUND)
    # Один скрипт без зависимостей; свой CMakeLists у проекта есть, но
    # конфигурировать нечего — отсюда SOURCE_SUBDIR в никуда.
    FetchContent_Declare(ctcache
      URL https://github.com/matus-chochlik/ctcache/archive/857b14271d3ded5511c2e6a60f2dc20c1e4b5680.tar.gz
      URL_HASH SHA256=1f3a4a39e0c1cacd1eb88e50be386a78ce8fed81ba516c8d88a67c4f85fd82a9
      SOURCE_SUBDIR do-not-configure)
    FetchContent_MakeAvailable(ctcache)

    # Версия анализатора — часть пути к кешу: ctcache её в ключ не берёт, а
    # новая версия вправе найти то, что старая пропустила.
    execute_process(COMMAND "${CAM_CLANG_TIDY}" --version
      OUTPUT_VARIABLE cam_clang_tidy_version)
    string(REGEX MATCH "version ([0-9.]+)" cam_clang_tidy_version
      "${cam_clang_tidy_version}")

    # Своя обёртка, а не `clang-tidy` из репозитория ctcache: та подхватывает
    # личные настройки из ~/.config/ctcache.
    set(cam_clang_tidy_binary "${CMAKE_BINARY_DIR}/clang-tidy-cached")
    file(WRITE "${cam_clang_tidy_binary}"
      "#!/bin/sh\n"
      "exec \"${Python3_EXECUTABLE}\" "
      "\"${ctcache_SOURCE_DIR}/src/ctcache/clang_tidy_cache.py\" "
      "\"${CAM_CLANG_TIDY}\" \"$@\"\n")
    file(CHMOD "${cam_clang_tidy_binary}" PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    set(cam_clang_tidy_environment
      "CTCACHE_LOCAL=1"
      "CTCACHE_DIR=${CMAKE_BINARY_DIR}/ctcache/${CMAKE_MATCH_1}")
  else()
    message(STATUS "clang-tidy или Python 3 не найден — clang-tidy идёт без кеша")
  endif()

  add_test(NAME clang_tidy
    COMMAND "${CAM_RUN_CLANG_TIDY}"
      -clang-tidy-binary "${cam_clang_tidy_binary}"
      -p "${CMAKE_BINARY_DIR}"
      -quiet
      -j ${cam_cpu_count}
      "^${CMAKE_SOURCE_DIR}/(libs|src|tests)/")

  set_tests_properties(clang_tidy PROPERTIES
    LABELS "lint;slow"
    TIMEOUT 1800
    ENVIRONMENT "${cam_clang_tidy_environment}")
else()
  message(STATUS "run-clang-tidy не найден — статический анализ пропущен")
endif()

# --- Сборка под санитайзерами -------------------------------------------------
# Санитайзерам нужен свой каталог сборки: их флаги с обычной сборкой не
# уживаются. А workflow-пресет CMake умеет ровно одну ступень конфигурации, то
# есть двумя каталогами в одном пресете не распорядиться.
#
# Поэтому вторая сборка оформлена тестом: `cmake --workflow --preset push-check`
# остаётся одной командой на все проверки. В самой санитарной сборке этот тест
# не регистрируется — иначе он вызывал бы сам себя без конца.
if(NOT REAPER_CAM_SANITIZE)
  add_test(NAME sanitize_suite
    COMMAND "${CMAKE_COMMAND}" --workflow --preset sanitize
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

  set_tests_properties(sanitize_suite PROPERTIES LABELS "slow" TIMEOUT 1800)
endif()
