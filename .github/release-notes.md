## Какие файлы для какой системы

| Система | Файл | Состояние |
|---|---|---|
| Linux x86_64 (glibc 2.35+) | `reaper_cam.so` | работает: камеры записываются |
| Windows 10/11 x64 | `reaper_cam.dll` | захват через Media Foundation: камеры с MJPEG, NV12 или YUY2 — практически любые; **на живых камерах ещё не проверен** |
| macOS 10.15+ (Apple Silicon и Intel, в том числе MacBook Pro 2015) | `reaper_cam.dylib` | захват через AVFoundation: встроенная камера, USB-камеры, iPhone как камера (macOS 14+); **на живых камерах ещё не проверен** |

На Linux нужна камера, которая отдаёт MJPEG: кадры пишутся без
перекодирования. На Windows и macOS годится практически любая: кадры камер без MJPEG
расширение сжимает в JPEG при записи.

Как поставить — в [руководстве](https://github.com/rodnover55/reaper-cam/blob/master/docs/install.md).
