# Tasks

## 1. Общее

- [x] 1.1 `cam/capture/backend_support.hpp`: режим из дроби частоты,
      сравнение частот, порядок режимов, перенос метки времени. Проверка —
      тесты `capture_backend_support_test.cpp`
- [x] 1.2 Захват V4L2 берёт порядок режимов из общего кода

## 2. Захват

- [x] 2.1 `libs/capture_mf`: Media Foundation (W1). Проверка — сборка Windows
      в CI
- [x] 2.2 `libs/capture_avf`: AVFoundation (W2). Проверка — сборка macOS в CI
- [x] 2.3 Ветки в CMake и `systemBackend()`
- [x] 2.4 Документация и описание релиза

## 3. Живая проверка

- [ ] 3.1 Windows: камера USB с MJPEG есть в списке с режимами; запись
      1280×720@30 даёт видео 1280×720, 30 кадров в секунду
- [ ] 3.2 Windows: в журнале отладочной сборки `timed by driver` растёт
      вместе с числом кадров
- [ ] 3.3 Windows: отключение камеры посреди дубля — `not connected` или
      `no frames`, дубль дописывается чёрным
- [ ] 3.4 macOS: первый arm спрашивает разрешение; после него запись идёт
- [ ] 3.5 macOS: камера USB с MJPEG записывается, а не останавливается с
      `camera delivers decoded frames only`; встроенная камера — «(no MJPEG)»
- [ ] 3.6 macOS: записанный MOV открывается в REAPER как видео
