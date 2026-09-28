# Утилиты для живых проверок захвата без REAPER (раздел 5 tasks.md).
add_executable(cam_probe cam_probe.cpp)
target_link_libraries(cam_probe PRIVATE cam_capture_v4l2 cam_container)
cam_enable_warnings(cam_probe)
