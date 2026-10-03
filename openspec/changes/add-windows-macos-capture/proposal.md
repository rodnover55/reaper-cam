# Proposal

## Why

Расширение записывает камеры только на Linux. Сборки для Windows и macOS уже
выходят в Releases, но камер не видят: дорожка-камера пишет чёрное видео.

## What Changes

- Захват на Windows через Media Foundation и на macOS через AVFoundation —
  вместо пустышки `UnsupportedBackend`. Как и на Linux: только режимы MJPEG,
  кадры без перекодирования, время кадра от системы, если она его сообщает.
- Общее для захвата всех ОС выносится в `cam_capture`: режим из дроби частоты
  с выравниванием округлённой длительности кадра, порядок режимов, перенос
  метки времени кадра из часов системы в часы расширения.
- Документация и описание релиза: что работает на какой системе.

Вне scope — как и прежде: перекодирование (встроенные камеры Mac без MJPEG не
поддерживаются), окно предпросмотра, звук камеры.

## Capabilities

### Modified Capabilities

- `camera-capture`: список камер, режимы, время съёмки и разрешение на камеру
  — на Linux, Windows и macOS.

## Impact

- **Новые цели**: `cam_capture_mf` (Windows: mfplat, mf, mfreadwrite,
  mfuuid, ole32) и `cam_capture_avf` (macOS: AVFoundation, CoreMedia,
  Foundation; Objective-C++ под ARC).
- **Проверка**: CI собирает и гоняет тесты на трёх ОС, но камер у сборщиков
  нет. Живая проверка на Windows и macOS — вручную (tasks.md, раздел 3).
