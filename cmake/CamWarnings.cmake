# Предупреждения компилятора для своих целей.
#
# Чужим исходникам (doctest, SDK и WDL) этот набор не достаётся намеренно:
# чинить там нечего, а их заголовки подключаются как SYSTEM, чтобы не шуметь в
# своей сборке.
#
# Превращать предупреждения в ошибки решает REAPER_CAM_WERROR: в пресетах он
# включён, при ручной сборке по умолчанию выключен — недописанный код не должен
# спотыкаться о неиспользованную переменную.

function(cam_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      $<$<BOOL:${REAPER_CAM_WERROR}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Woverloaded-virtual
      -Wcast-align
      -Wdouble-promotion
      -Wformat=2
      -Wimplicit-fallthrough
      -Wconversion
      -Wsign-conversion
      $<$<BOOL:${REAPER_CAM_WERROR}>:-Werror>)
  endif()
endfunction()
