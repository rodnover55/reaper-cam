# Списки изоляции библиотеки cam_capture (design.md D9). Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/CamIsolation.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CamIsolation.cmake")

set(isolation_name "захват")

set(forbidden_patterns ${cam_forbidden_patterns})

# LICE и jpeglib из WDL разрешены: ими тестовый источник рисует и сжимает кадр
# (design.md D10). <cstdio> нужен jpeglib: его заголовок ждёт FILE.
set(allowed_includes
  "cam/capture/"
  "WDL/lice/"
  "WDL/jpeglib/"
  "fmt/"
  "cstdio"
  ${cam_standard_includes})
