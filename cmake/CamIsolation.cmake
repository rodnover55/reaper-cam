# Общая часть списков изоляции: то, что запрещено во всех трёх независимых
# библиотеках (design.md D9). Подключается из `libs/<имя>/isolation.cmake`,
# а те дописывают своё.

# Понятия хоста, ОС и их библиотек. Заголовки ловит список разрешённых
# включений; здесь — то, что пролезает мимо него: имена API в коде и ветки под
# конкретную ОС.
set(cam_forbidden_patterns
  "[Rr][Ee][Aa][Pp][Ee][Rr]"
  "PCM_"
  "ReaSample"
  "MediaTrack"
  "HWND"
  "SWELL"
  "[Vv]4[Ll]2"
  "VIDIOC"
  "[Uu][Vv][Cc]"
  "ioctl"
  "clock_gettime"
  "QueryPerformance"
  "mach_absolute_time"
  "_WIN32"
  "__APPLE__"
  "__linux__")

# Стандартная библиотека. Заголовков C из неё (<unistd.h> и прочих POSIX)
# в списке нет намеренно: это уже ОС.
set(cam_standard_includes
  "algorithm" "array" "atomic" "bit" "cassert" "cctype" "charconv" "chrono" "cmath"
  "compare" "condition_variable" "cstddef" "cstdint" "cstring" "deque" "exception" "format" "functional" "limits"
  "map" "memory" "mutex" "numeric" "optional" "ranges" "span" "stdexcept" "string"
  "string_view" "thread" "type_traits" "utility" "vector")

# Внутренние заголовки библиотеки лежат рядом с исходниками в src/ и
# включаются без каталога: "bytes.hpp".
set(cam_local_includes "[a-z_]+\\.hpp$")
