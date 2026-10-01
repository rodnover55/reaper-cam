#include <doctest/doctest.h>

#include "cam/capture/unsupported_backend.hpp"

using cam::capture::CameraMode;
using cam::capture::CaptureError;
using cam::capture::UnsupportedBackend;

TEST_CASE("пустышка захвата: камер нет, и она говорит почему") {
  UnsupportedBackend backend("Windows");

  CHECK(backend.list().empty());
  CHECK(backend.unsupported() == "Camera capture on Windows is not supported yet.");
}

TEST_CASE("пустышка захвата: открыть камеру нельзя — ошибка устройства") {
  UnsupportedBackend backend("macOS");

  const CameraMode mode{.width = 1280, .height = 720};
  CHECK_THROWS_WITH_AS((void)backend.open("by-id/usb-camera-video-index0", mode),
                       "capture is not supported on macOS", CaptureError);
}
