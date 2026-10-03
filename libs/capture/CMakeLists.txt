# Интерфейс источника кадров, общее для захвата всех ОС, реестр устройств,
# тестовый источник и пустышка для ОС без захвата. От ОС и хоста не зависит
# (design.md D9): это проверяет тест capture_isolation.
#
# LICE и jpeglib из WDL — не ОС и не хост: тестовый источник рисует ими кадр
# и сжимает его (design.md D10). Наружу они не видны: заголовки библиотеки
# говорят байтами и числами.
add_library(cam_capture STATIC
  src/backend_support.cpp
  src/device_registry.cpp
  src/jpeg_encoder.cpp
  src/test_pattern.cpp
  src/unsupported_backend.cpp)

target_include_directories(cam_capture PUBLIC include)

target_link_libraries(cam_capture PRIVATE wdl_lice)

# Поток захвата на камеру (design.md D8).
find_package(Threads REQUIRED)
target_link_libraries(cam_capture PUBLIC Threads::Threads)

cam_enable_warnings(cam_capture)
