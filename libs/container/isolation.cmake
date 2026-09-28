# Списки изоляции библиотеки cam_container (design.md D9). Механика — в
# cmake/CheckIsolation.cmake, общая часть списков — в cmake/CamIsolation.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CamIsolation.cmake")

set(isolation_name "контейнер")

set(forbidden_patterns ${cam_forbidden_patterns})

# Файл пишется средствами стандартной библиотеки: <fstream> и <filesystem> —
# не ОС, а их переносимая обёртка.
set(allowed_includes
  "cam/container/"
  "fstream" "filesystem" "ios"
  ${cam_local_includes}
  ${cam_standard_includes})
