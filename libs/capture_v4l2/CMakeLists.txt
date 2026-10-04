# Захват Linux: V4L2 и метаданные UVC (design.md D6). Единственная
# библиотека, которой можно знать про ОС; наружу — интерфейсы cam_capture.
add_library(cam_capture_v4l2 STATIC
  src/capture.cpp
  src/device_list.cpp
  src/uvc_clock.cpp)

target_include_directories(cam_capture_v4l2 PUBLIC include)
target_link_libraries(cam_capture_v4l2 PUBLIC cam_capture PRIVATE fmt::fmt)

cam_enable_warnings(cam_capture_v4l2)
