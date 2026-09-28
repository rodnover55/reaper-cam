# Писатель контейнера и дополнение таблиц JPEG. От ОС и хоста не зависит
# (design.md D9): это проверяет тест container_isolation.
add_library(cam_container STATIC
  src/jpeg_tables.cpp
  src/mov_writer.cpp
  src/output.cpp
  src/writer.cpp)

target_include_directories(cam_container PUBLIC include)

cam_enable_warnings(cam_container)
