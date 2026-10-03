# Захват Windows: Media Foundation. Единственная библиотека, которой можно
# знать про ОС; наружу — интерфейсы cam_capture.
add_library(cam_capture_mf STATIC
  src/media_foundation.cpp)

target_include_directories(cam_capture_mf PUBLIC include)
target_link_libraries(cam_capture_mf
  PUBLIC cam_capture
  PRIVATE mfplat mf mfreadwrite mfuuid ole32)

cam_enable_warnings(cam_capture_mf)
