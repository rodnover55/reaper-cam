# Proposal

## Why

Расширение записывает камеры только на Linux. Сборки для Windows и macOS уже
выходят в Releases, но камер не видят: дорожка-камера пишет чёрное видео.

## What Changes

- Захват на Windows через Media Foundation и на macOS через AVFoundation —
  вместо пустышки `UnsupportedBackend`. Время кадра от системы, если она его
  сообщает.
- Камеры без MJPEG на Windows и macOS тоже записываются: их кадры (NV12,
  YUY2) расширение сжимает в JPEG при захвате — на macOS кодером VideoToolbox,
  на Windows jpeglib. Без этого на Mac не работает встроенная камера, а это
  камера, которая есть у каждого. Кадры MJPEG по-прежнему пишутся без
  пережатия, а если режим есть и в MJPEG, и без сжатия, берётся MJPEG.
- Общее для захвата всех ОС выносится в `cam_capture`: режим из дроби частоты
  с выравниванием округлённой длительности кадра, порядок режимов, перенос
  метки времени кадра из часов системы в часы расширения.
- Документация и описание релиза: что работает на какой системе.

Вне scope: сжатие кадров камер без MJPEG на Linux (там почти все камеры
отдают MJPEG; захват V4L2 не меняется), окно предпросмотра, звук камеры.

## Capabilities

### Modified Capabilities

- `camera-capture`: список камер, режимы, время съёмки и разрешение на камеру
  — на Linux, Windows и macOS.

## Impact

- **Новые цели**: `cam_capture_mf` (Windows: mfplat, mf, mfreadwrite,
  mfuuid, ole32) и `cam_capture_avf` (macOS: AVFoundation, CoreMedia,
  CoreVideo, Foundation, VideoToolbox; Objective-C++ под ARC).
- **`cam_capture`**: программное сжатие кадров NV12 и YUY2 в JPEG и режимы из
  диапазона частот — без зависимостей от ОС, с тестами.
- **Проверка**: CI собирает и гоняет тесты на трёх ОС, но камер у сборщиков
  нет. Живая проверка на Windows и macOS — вручную (tasks.md, раздел 3).
