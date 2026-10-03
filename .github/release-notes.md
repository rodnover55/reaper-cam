## Какие файлы для какой системы

| Система | Файл | Состояние |
|---|---|---|
| Linux x86_64 (glibc 2.35+) | `reaper_cam.so` | работает: камеры записываются |
| Windows 10/11 x64 | `reaper_cam.dll` | захват через Media Foundation; **на живых камерах ещё не проверен** |
| macOS 13.3+ (Apple Silicon и Intel) | `reaper_cam.dylib` | захват через AVFoundation; **на живых камерах ещё не проверен**; встроенные камеры Mac не отдают MJPEG и не поддерживаются |

На всех системах нужны камеры, которые отдают MJPEG: кадры пишутся без
перекодирования.

Как поставить — в [руководстве](https://github.com/rodnover55/reaper-cam/blob/master/docs/install.md).
