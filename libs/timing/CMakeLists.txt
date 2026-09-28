# Мост часов и сетка кадров. От ОС и хоста не зависит (design.md D9): это
# проверяет тест timing_isolation.
add_library(cam_timing STATIC
  src/clock_bridge.cpp
  src/frame_grid.cpp)

target_include_directories(cam_timing PUBLIC include)

cam_enable_warnings(cam_timing)
