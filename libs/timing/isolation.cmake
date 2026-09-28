# Списки изоляции библиотеки cam_timing (design.md D9). Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/CamIsolation.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CamIsolation.cmake")

set(isolation_name "время")

set(forbidden_patterns ${cam_forbidden_patterns})

set(allowed_includes
  "cam/timing/"
  ${cam_standard_includes})
