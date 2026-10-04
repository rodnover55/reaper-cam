# Захват macOS: AVFoundation. Единственная библиотека, которой можно знать
# про ОС; наружу — интерфейсы cam_capture. Исходник на Objective-C++, заголовок
# на чистом C++.
add_library(cam_capture_avf STATIC
  src/avfoundation.mm
  src/frame_compressor.mm)

target_include_directories(cam_capture_avf PUBLIC include)
target_link_libraries(cam_capture_avf
  PUBLIC cam_capture
  PRIVATE
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework Foundation"
    "-framework VideoToolbox"
    fmt::fmt)

# Объекты Objective-C живут под ARC: держатели в классе C++ освобождают их
# сами.
target_compile_options(cam_capture_avf PRIVATE -fobjc-arc)

cam_enable_warnings(cam_capture_avf)
