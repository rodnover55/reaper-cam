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
| `cmake --workflow --preset release` | Собрать модуль так, как его собирает CI для Releases, и прогнать тесты. Файл — в `build/release`. |

Полный список — `cmake --list-presets=<configure|build|test|workflow>`. Личные
пресеты кладутся в `CMakeUserPresets.json`, он в репозиторий не попадает.

## Сборки и релизы

GitHub Actions (`.github/workflows/build.yml`) на каждый push собирает модуль
пресетом `release` на трёх системах и прогоняет тесты:

| Система | Где собирается | Файл |
|---|---|---|
| Linux x86_64 | Ubuntu 22.04, GCC 13 | `reaper_cam.so` |
| Windows x64 | MSVC | `reaper_cam.dll` |
| macOS 10.15+, arm64 и x86_64 одним файлом | Apple clang | `reaper_cam.dylib` |

Файлы лежат в артефактах прогона. Линтеры в CI не идут: их результат зависит
от версии инструментов, и они остаются в `push-check`.

Захват камер у каждой ОС свой: `libs/capture_v4l2` (Linux, V4L2),
`libs/capture_mf` (Windows, Media Foundation), `libs/capture_avf` (macOS,
AVFoundation). Общее для них — в `cam/capture/backend_support.hpp`: режимы из
частоты, которую сообщает система, и перенос метки времени кадра в часы
расширения. На прочих ОС модуль собирается с пустышкой
`capture::UnsupportedBackend`: формат и окно настроек работают, камер нет,
дубль чёрный. Захват для новой ОС — своя библиотека рядом с этими и ветка в
`systemBackend()` (`src/services.cpp`).

Захват Windows и macOS CI только собирает: камер у сборщиков нет, и живьём
его нужно проверять на своей машине.

Чтобы модуль грузился на чужих машинах, пресет `release` включает
`REAPER_CAM_STATIC_RUNTIME`: на Linux libstdc++ вкомпонована и скрыта
(`cmake/reaper_cam.map`), на Windows рантайм MSVC статический.

Релиз:

1. Поднять версию в `project()` в `CMakeLists.txt` и закоммитить.
2. Поставить тег с той же версией и отправить его:
   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

Тег запускает ту же сборку и после неё создаёт релиз с тремя файлами и
`SHA256SUMS.txt`. Описание — `.github/release-notes.md` (что на какой системе
работает; поправить, когда появится захват для Windows или macOS) и список
коммитов и PR, собранный GitHub. Если тег не совпадает
с версией в `CMakeLists.txt`, релиз не создаётся. Тег с суффиксом
(`v0.2.0-rc1`) даёт предварительный релиз.

## Инструменты

Для сборки нужны компилятор, CMake и Ninja. REAPER SDK, WDL, {fmt} и doctest
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
