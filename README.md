# reaper-cam

Расширение (extension plugin) для REAPER на C++: запись видео с камеры при
обычной записи в REAPER.

Как пользоваться расширением — установка, запись, настройки, сообщения —
в [руководстве пользователя](docs/README.md). Ниже — сборка и проверки для
разработки.

## Команды

| Команда | Для чего |
|---|---|
| `cmake --preset default` | Подготовить сборку. Нужно один раз в свежем клоне и после правок в `CMakeLists.txt`. |
| `cmake --build --preset default` | Собрать плагин и тесты. |
| `ctest --preset quick` | Проверить правку на ходу — без долгих линтеров и сборки под санитайзерами. |
| `cmake --workflow --preset push-check` | Полная проверка перед push. |
| `cmake --build build --target format` | Привести форматирование в порядок, когда упал `clang_format`. |
| `cmake --workflow --preset sanitize` | Прогнать тесты под ASan и UBSan, если есть подозрение на порчу памяти. |
| `cmake --install build` | Поставить плагин в REAPER и попробовать вживую. |

Полный список — `cmake --list-presets=<configure|build|test|workflow>`. Личные
пресеты кладутся в `CMakeUserPresets.json`, он в репозиторий не попадает.

## Инструменты

Для сборки нужны компилятор, CMake и Ninja. REAPER SDK, WDL и doctest
скачиваются при конфигурации.

Остальные проверки подключаются сами, если инструмент есть в системе, и
пропускаются, если нет:

```sh
sudo apt install clang clang-tidy clang-format
```

clang-tidy идёт через кеш результатов [ctcache](https://github.com/matus-chochlik/ctcache):
неизменённый файл не анализируется заново, поэтому полный обход занимает минуты
только в первый раз. Скрипту нужен Python 3; без Python clang-tidy работает без
кеша. Кеш лежит в `build/ctcache/<версия clang-tidy>`; сбросить — удалить этот
каталог.

## Снимки для руководства

Снимки в `docs/images` сделаны отладочной сборкой (`cmake --build --preset
debug`) с вымышленными камерами: если запустить REAPER с переменной окружения
`REAPER_CAM_DEMO_CAMERAS=1`, расширение вместо камер системы показывает «USB
Camera» с рисованной сценой вместо изображения и «Old USB Camera» без MJPEG.
Настоящие камеры в этом режиме не перечисляются и не открываются.
