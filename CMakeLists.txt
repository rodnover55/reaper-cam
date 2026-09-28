cmake_minimum_required(VERSION 3.25)

project(reaper-cam
  VERSION 0.1.0
  DESCRIPTION "REAPER extension: camera recording"
  LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(REAPER_CAM_BUILD_TESTS "Собирать тесты (REAPER и камера для них не нужны)" ON)
option(REAPER_CAM_WERROR "Считать предупреждения компилятора ошибками" OFF)

set(REAPER_CAM_SANITIZE "" CACHE STRING
  "Санитайзеры списком для -fsanitize, например address,undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CamWarnings)

# --- Санитайзеры --------------------------------------------------------------
# Флаги ставятся до объявления целей и поэтому достаются всем, включая чужой
# код из WDL: ASan видит только то, что инструментировано.
#
# -fno-sanitize-recover=all обязателен: по умолчанию UBSan печатает замечание и
# продолжает, то есть тест проходит при найденной ошибке.
if(REAPER_CAM_SANITIZE)
  add_compile_options(
    "-fsanitize=${REAPER_CAM_SANITIZE}"
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g)
  add_link_options("-fsanitize=${REAPER_CAM_SANITIZE}")
endif()

# --- Зависимости --------------------------------------------------------------
# SDK и WDL скачиваются в каталог сборки, в репозиторий ничего не попадает.
# Версии зафиксированы по коммиту: у обоих репозиториев нет релизных тегов.
# Коммиты те же, что у reaper-tabs: оба расширения живут в одном REAPER.
include(FetchContent)

FetchContent_Declare(reaper_sdk
  URL https://github.com/justinfrankel/reaper-sdk/archive/08e885d14c87c2973ec301d220ff9fe6b6a7cbeb.tar.gz
  URL_HASH SHA256=506e1a3225d6717aa42a4a57637cf413c5a35f5956639bde22b946af96b597e4)

FetchContent_Declare(wdl
  URL https://github.com/justinfrankel/WDL/archive/8f4d783de745126ac8c201455dc30818c8613324.tar.gz
  URL_HASH SHA256=19820bfa25e494b743903ac9b84c3ce0148ae83697ee91045d9b2ac6ba3f78a5)

FetchContent_MakeAvailable(reaper_sdk wdl)

if(REAPER_CAM_BUILD_TESTS)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  # Иначе `cmake --install` полез бы ставить заголовки doctest в /usr/local.
  # Ставится отсюда ровно одно — модуль расширения в UserPlugins.
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)

  # doctest.h включает <ciso646>, а libstdc++ 15 встречает его директивой
  # #warning: с C++20 заголовок нестандартный. При -Werror clang считает это
  # ошибкой (gcc — нет). Помечаем заголовки doctest системными: вместе с ними
  # замолкает и всё, что они за собой тянут.
  set_target_properties(doctest PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${doctest_SOURCE_DIR}")
endif()

# reaper_plugin.h подключает swell как "../WDL/swell/swell.h", то есть ждёт WDL
# соседом каталога sdk/. Кладём туда ссылку на распакованный WDL.
if(NOT EXISTS "${reaper_sdk_SOURCE_DIR}/WDL")
  file(CREATE_LINK "${wdl_SOURCE_DIR}/WDL" "${reaper_sdk_SOURCE_DIR}/WDL"
    RESULT link_status SYMBOLIC)
  if(NOT link_status EQUAL 0)
    file(COPY "${wdl_SOURCE_DIR}/WDL" DESTINATION "${reaper_sdk_SOURCE_DIR}")
  endif()
endif()

# --- JPEG и LICE из WDL -------------------------------------------------------
# jpeglib сжимает кадры тестового источника и чёрный кадр, LICE рисует
# тестовый кадр и разжимает JPEG в тестах (design.md D7, D10). Отдельной
# зависимости нет: всё лежит в уже скачанном WDL.
#
# Код чужой и старый — предупреждения сняты, как у заглушки SWELL ниже.
set(wdl_jpeg_dir "${wdl_SOURCE_DIR}/WDL/jpeglib")

add_library(wdl_jpeg STATIC
  "${wdl_jpeg_dir}/jcapimin.c" "${wdl_jpeg_dir}/jcapistd.c" "${wdl_jpeg_dir}/jccoefct.c"
  "${wdl_jpeg_dir}/jccolor.c" "${wdl_jpeg_dir}/jcdctmgr.c" "${wdl_jpeg_dir}/jchuff.c"
  "${wdl_jpeg_dir}/jcinit.c" "${wdl_jpeg_dir}/jcmainct.c" "${wdl_jpeg_dir}/jcmarker.c"
  "${wdl_jpeg_dir}/jcmaster.c" "${wdl_jpeg_dir}/jcomapi.c" "${wdl_jpeg_dir}/jcparam.c"
  "${wdl_jpeg_dir}/jcphuff.c" "${wdl_jpeg_dir}/jcprepct.c" "${wdl_jpeg_dir}/jcsample.c"
  "${wdl_jpeg_dir}/jdapimin.c" "${wdl_jpeg_dir}/jdapistd.c" "${wdl_jpeg_dir}/jdatasrc.c"
  "${wdl_jpeg_dir}/jdcoefct.c" "${wdl_jpeg_dir}/jdcolor.c" "${wdl_jpeg_dir}/jddctmgr.c"
  "${wdl_jpeg_dir}/jdhuff.c" "${wdl_jpeg_dir}/jdinput.c" "${wdl_jpeg_dir}/jdmainct.c"
  "${wdl_jpeg_dir}/jdmarker.c" "${wdl_jpeg_dir}/jdmaster.c" "${wdl_jpeg_dir}/jdmerge.c"
  "${wdl_jpeg_dir}/jdphuff.c" "${wdl_jpeg_dir}/jdpostct.c" "${wdl_jpeg_dir}/jdsample.c"
  "${wdl_jpeg_dir}/jerror.c" "${wdl_jpeg_dir}/jfdctflt.c" "${wdl_jpeg_dir}/jfdctfst.c"
  "${wdl_jpeg_dir}/jfdctint.c" "${wdl_jpeg_dir}/jidctflt.c" "${wdl_jpeg_dir}/jidctfst.c"
  "${wdl_jpeg_dir}/jidctint.c" "${wdl_jpeg_dir}/jidctred.c" "${wdl_jpeg_dir}/jmemmgr.c"
  "${wdl_jpeg_dir}/jmemnobs.c" "${wdl_jpeg_dir}/jquant1.c" "${wdl_jpeg_dir}/jquant2.c"
  "${wdl_jpeg_dir}/jutils.c")

target_include_directories(wdl_jpeg SYSTEM PUBLIC "${wdl_SOURCE_DIR}")

# -fexceptions: ошибку jpeglib свой код превращает в исключение C++, и оно
# раскручивается через кадры jpeglib. Без таблиц раскрутки в коде на C это
# std::terminate (libs/capture/src/jpeg_encoder.cpp).
target_compile_options(wdl_jpeg PRIVATE -w -fexceptions)

# jpeglib 6b сдвигает влево отрицательные числа (jfdctint.c и соседи):
# формально это неопределённое поведение, на деле — арифметика, на которую код
# и рассчитан. UBSan честно останавливается на первом же кадре. Код чужой, как
# и с предупреждениями выше; остальные проверки UBSan для него остаются.
if(REAPER_CAM_SANITIZE MATCHES "undefined")
  target_compile_options(wdl_jpeg PRIVATE -fno-sanitize=shift)
endif()

# Из LICE берутся растровые операции, встроенный шрифт 8 px (lice_text.cpp:
# системных шрифтов он не требует и поэтому работает в тестах) и разжатие JPEG.
#
# _LICE_NO_SYSBITMAPS_ убирает LICE_SysBitmap — единственное, что тянет функции
# SWELL. Без него тестам, которые идут без REAPER, пришлось бы подкладывать
# заглушку SWELL.
set(wdl_lice_dir "${wdl_SOURCE_DIR}/WDL/lice")

add_library(wdl_lice STATIC
  "${wdl_lice_dir}/lice.cpp"
  "${wdl_lice_dir}/lice_text.cpp"
  "${wdl_lice_dir}/lice_jpg.cpp")

target_include_directories(wdl_lice SYSTEM PUBLIC "${wdl_SOURCE_DIR}")
target_compile_definitions(wdl_lice PUBLIC _LICE_NO_SYSBITMAPS_ WDL_NO_DEFINE_MINMAX)
target_compile_options(wdl_lice PRIVATE -w)
target_link_libraries(wdl_lice PUBLIC wdl_jpeg)

# --- Библиотеки ---------------------------------------------------------------
# Всё, что можно собрать и проверить без REAPER, без камеры и без заголовков ОС
# (design.md D9). Друг от друга они не зависят; модуль расширения собирает их
# вместе.
add_subdirectory(libs/timing)
add_subdirectory(libs/container)
add_subdirectory(libs/capture)

# Захват зависит от ОС (design.md D9). В этом изменении он есть только для
# Linux; ветки Windows и macOS появятся вместе с их модулями захвата.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(libs/capture_v4l2)
  add_subdirectory(tools)
endif()

# --- Ресурсы форм -------------------------------------------------------------
# Формы описаны в src/views/forms.rc — одним исходником на все платформы.
# Под SWELL из него порождается таблица C++; на Windows тот же файл собрал бы
# компилятор ресурсов, поэтому шаг ниже только для не-Windows.
#
# Генератор пишет результат рядом с входным файлом, поэтому .rc и ids.h
# сначала копируются в каталог сборки — иначе порождённое насорит в src/.
if(NOT WIN32)
  find_package(Perl REQUIRED)

  set(swell_res_dir "${CMAKE_CURRENT_BINARY_DIR}/swellres")
  set(swell_res_dlg "${swell_res_dir}/forms.rc_mac_dlg")

  add_custom_command(
    OUTPUT "${swell_res_dlg}"
    BYPRODUCTS "${swell_res_dir}/forms.rc_mac_menu"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${swell_res_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/forms.rc" "${swell_res_dir}/forms.rc"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/ids.h" "${swell_res_dir}/ids.h"
    COMMAND "${PERL_EXECUTABLE}" "${wdl_SOURCE_DIR}/WDL/swell/swell_resgen.pl"
            "${swell_res_dir}/forms.rc"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/views/forms.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/views/ids.h"
    COMMENT "Порождение таблицы форм SWELL из views/forms.rc"
    VERBATIM)

  add_custom_target(reaper_cam_resources DEPENDS "${swell_res_dlg}")
endif()

# --- Плагин -------------------------------------------------------------------
add_library(reaper_cam MODULE
  src/main.cpp
  src/arm_watch.cpp
  src/audio_clock.cpp
  src/debug_actions.cpp
  src/format.cpp
  src/format_choice.cpp
  src/format_config.cpp
  src/journal.cpp
  src/services.cpp
  src/sink.cpp
  src/take.cpp
  src/test_source.cpp
  src/views/format_config.cpp
  src/views/resources.cpp)

# Свои заголовки включаются от корня src/.
target_include_directories(reaper_cam PRIVATE src)

# Журнал вызовов REAPER и тестовый источник кадров есть только в отладочной
# сборке (design.md D10): пользователю они не показываются.
target_compile_definitions(reaper_cam PRIVATE $<$<CONFIG:Debug>:CAM_DEBUG_BUILD>)

# Отметка времени сборки, которую расширение печатает при загрузке. По ней
# видно, тот ли модуль в REAPER: не поставленный вовремя плагин выглядит как
# «поведение не изменилось», и отличить это от настоящей ошибки иначе нечем.
#
# Цель обновляется на каждой сборке, поэтому одна единица трансляции
# пересобирается всегда — цена отметки, которая не врёт.
set(cam_build_stamp "${CMAKE_BINARY_DIR}/build_stamp.hpp")

add_custom_target(reaper_cam_stamp
  BYPRODUCTS "${cam_build_stamp}"
  COMMAND "${CMAKE_COMMAND}" -D "OUTPUT=${cam_build_stamp}"
    -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildStamp.cmake"
  COMMENT "Отметка времени сборки")

add_dependencies(reaper_cam reaper_cam_stamp)
target_include_directories(reaper_cam PRIVATE "${CMAKE_BINARY_DIR}")

target_link_libraries(reaper_cam PRIVATE cam_capture cam_capture_v4l2 cam_container cam_timing)

# Таблица диалога порождена чужим скриптом: её агрегаты инициализированы не
# полностью, и clang считает это ошибкой при нашем наборе предупреждений.
set_source_files_properties(src/views/resources.cpp PROPERTIES COMPILE_OPTIONS "-w")

if(NOT WIN32)
  add_dependencies(reaper_cam reaper_cam_resources)
  target_include_directories(reaper_cam PRIVATE "${swell_res_dir}")
endif()

# SYSTEM: SDK и WDL чужие, их предупреждения не наши.
target_include_directories(reaper_cam SYSTEM PRIVATE
  "${reaper_sdk_SOURCE_DIR}/sdk"
  "${wdl_SOURCE_DIR}")

cam_enable_warnings(reaper_cam)

# Неразрешённый символ иначе доезжает до REAPER и виден только как статус
# "Error" в списке расширений — без имени символа и без намёка на причину.
# Флаг переносит эту ошибку на линковку, где видно и то и другое.
#
# Под санитайзерами он неприменим: там часть символов рантайма остаётся
# неразрешённой намеренно, их подставляет процесс, который модуль загрузит.
if(NOT REAPER_CAM_SANITIZE)
  target_link_options(reaper_cam PRIVATE -Wl,--no-undefined)
endif()

# REAPER ищет в UserPlugins файлы вида reaper_*.so (.dylib на macOS, .dll на Windows).
set_target_properties(reaper_cam PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_cam")

if(APPLE)
  set_target_properties(reaper_cam PROPERTIES SUFFIX ".dylib")
endif()

if(NOT WIN32)
  # Реализация SWELL живёт в самом REAPER; модуль получает её через тонкую
  # заглушку, поэтому GTK-заголовки для сборки не нужны.
  target_compile_definitions(reaper_cam PRIVATE SWELL_PROVIDED_BY_APP)
  if(APPLE)
    set(swell_stub "${wdl_SOURCE_DIR}/WDL/swell/swell-modstub.mm")
  else()
    set(swell_stub "${wdl_SOURCE_DIR}/WDL/swell/swell-modstub-generic.cpp")
  endif()

  target_sources(reaper_cam PRIVATE "${swell_stub}")

  # Заглушка чужая и собирается в составе своей цели, то есть общий набор
  # предупреждений достаётся и ей. Снимаем: исправить её мы всё равно не можем.
  set_source_files_properties("${swell_stub}" PROPERTIES COMPILE_OPTIONS "-w")
endif()

# --- Установка ----------------------------------------------------------------
if(WIN32)
  set(REAPER_RESOURCE_DIR "$ENV{APPDATA}/REAPER")
elseif(APPLE)
  set(REAPER_RESOURCE_DIR "$ENV{HOME}/Library/Application Support/REAPER")
else()
  set(REAPER_RESOURCE_DIR "$ENV{HOME}/.config/REAPER")
endif()

set(REAPER_USER_PLUGINS "${REAPER_RESOURCE_DIR}/UserPlugins")

install(TARGETS reaper_cam LIBRARY DESTINATION "${REAPER_USER_PLUGINS}")

# --- Тесты --------------------------------------------------------------------
if(REAPER_CAM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
