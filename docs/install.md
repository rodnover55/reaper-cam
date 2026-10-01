# Установка

## Что нужно

| Что | Зачем и какое |
|---|---|
| Linux | Камеры расширение пока записывает только на Linux (x86_64) — с glibc 2.35 и новее: Ubuntu 22.04, Debian 12, Fedora 36 и всё, что свежее. Сборки для Windows (x64) и macOS (13.3 и новее, Apple Silicon и Intel) ставятся и загружаются, но камер не видят: дорожка-камера на них пишет чёрное видео (см. [Windows и macOS](#windows-и-macos)). |
| REAPER 7 | Проверено на REAPER 7.80 для Linux (x86_64). |
| USB-камера с MJPEG | Любая обычная веб-камера USB (UVC), которая умеет отдавать MJPEG, — таких большинство, включая встроенные в ноутбуки. Камеры, которые отдают только несжатую картинку, не поддерживаются. |
| Видео в REAPER | Чтобы смотреть записанное видео в REAPER, в системе нужны библиотеки FFmpeg: REAPER на Linux показывает видео через них. Обычно они ставятся пакетом `ffmpeg`. |
| Место на диске | Около 5 МБ в секунду при 1280x720 — порядка 18 ГБ на час записи (подробнее — в разделе [Запись](recording.md#сколько-места-занимает-видео)). |

Доступ к камере. Программы получают камеру через устройство `/dev/video*`.
В обычном настольном Linux доступ к нему у пользователя есть сразу. Если
камера не видна ни в одной программе, проверьте, что пользователь входит в
группу `video`.

## Как поставить

1. Скачайте файл расширения для своей системы со страницы
   [Releases](https://github.com/rodnover55/reaper-cam/releases/latest):

   | Система | Файл | Папка `UserPlugins` |
   |---|---|---|
   | Linux (x86_64) | `reaper_cam.so` | `~/.config/REAPER/UserPlugins/` |
   | Windows (x64) | `reaper_cam.dll` | `%APPDATA%\REAPER\UserPlugins\` |
   | macOS (Apple Silicon и Intel) | `reaper_cam.dylib` | `~/Library/Application Support/REAPER/UserPlugins/` |

   Если REAPER стоит в портативном режиме, папка ресурсов — другая: её
   открывает пункт меню **Options → Show REAPER resource path in
   explorer/finder**.
2. Закройте REAPER.
3. Положите файл в папку `UserPlugins` папки ресурсов REAPER. Если папки нет —
   создайте её.
4. Только macOS: снимите с файла пометку «скачано из интернета», иначе
   система не даст REAPER его загрузить. В Терминале:

   ```sh
   xattr -d com.apple.quarantine ~/Library/Application\ Support/REAPER/UserPlugins/reaper_cam.dylib
   ```

5. Запустите REAPER.

Файл можно и собрать из исходников проекта. Нужны компилятор C++, CMake и
Ninja; в папке проекта выполните:

```sh
cmake --preset default
cmake --build --preset default
cmake --install build
```

Последняя команда сама кладёт файл расширения в папку `UserPlugins`.

## Как проверить, что расширение загрузилось

При запуске REAPER расширение пишет строку в консоль REAPER (окно «ReaScript
console output»):

![Строка о загрузке расширения в консоли REAPER](images/console-loaded.png)

В строке — версия расширения (`reaper-cam 0.1.0 loaded`), в скобках — дата и
время сборки: по ним видно, что загружено.

Второй признак — в списке форматов записи дорожки появился «Video (camera)»
(как его найти — в разделе [Первая запись](quick-start.md)).

## Windows и macOS

Захват камер пока написан только для Linux. На Windows и macOS расширение
всё равно загружается, а сразу после строки о загрузке пишет в консоль:

`Camera capture on Windows is not supported yet. Camera tracks record black video.`

(на macOS — `on macOS`). Формат «Video (camera)» и его окно настроек есть, но
список камер пуст, а дорожка-камера записывает чёрное видео нужной длины.
Звук записывается как обычно.

## Обновление

Закройте REAPER, замените файл расширения новым со страницы
[Releases](https://github.com/rodnover55/reaper-cam/releases/latest) и
запустите REAPER снова. Настройки дорожек-камер хранятся в проектах и после
обновления остаются.

## Удаление

Закройте REAPER и удалите файл расширения из папки `UserPlugins`
(`reaper_cam.so`, `reaper_cam.dll` или `reaper_cam.dylib`). Записанные видео
остаются обычными файлами MOV и открываются в REAPER и других программах и
без расширения. А вот записывать дорожками-камерами без расширения REAPER не
сможет: формата «Video (camera)» он не знает.
