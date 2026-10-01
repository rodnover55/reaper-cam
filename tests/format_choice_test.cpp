#include <doctest/doctest.h>

#include "format_choice.hpp"

#include <string>
#include <vector>

using cam::capture::CameraInfo;
using cam::capture::CameraMode;
using cam::reaper::cameraLabel;
using cam::reaper::FormatChoice;
using cam::reaper::FormatConfig;
using cam::reaper::modeLabel;

namespace {

constexpr CameraMode k720{
    .width = 1280, .height = 720, .rateNumerator = 30, .rateDenominator = 1};
constexpr CameraMode k360{
    .width = 640, .height = 360, .rateNumerator = 30, .rateDenominator = 1};

CameraInfo laptop() {
  return {.id = "by-id/usb-CN09357G8LG009BLAFRPA01_Integrated_Webcam_HD-video-index0",
          .name = "Integrated_Webcam_HD",
          .modes = {k720, k360}};
}

/// Камера, которая отдаёт только YUYV: режимов MJPEG у неё нет.
CameraInfo rawOnly() {
  return {.id = "by-id/usb-raw-video-index0", .name = "Raw Cam", .modes = {}};
}

} // namespace

TEST_CASE("выбор формата: камера не сохранена — первая с MJPEG и её первый режим") {
  const FormatChoice choice({rawOnly(), laptop()}, FormatConfig{});

  REQUIRE(choice.camera());
  CHECK(*choice.camera() == 1);
  CHECK(choice.config() ==
        FormatConfig{.cameraId = laptop().id, .cameraName = laptop().name, .mode = k720});
  CHECK(choice.status().empty());
}

TEST_CASE("выбор формата: сохранённые камера и режим восстанавливаются") {
  const FormatConfig saved{.cameraId = laptop().id, .cameraName = laptop().name, .mode = k360};
  const FormatChoice choice({laptop()}, saved);

  CHECK(choice.mode() == 1U);
  CHECK(choice.config() == saved);
}

TEST_CASE("выбор формата: сохранённой камеры нет в системе — выбор не теряется") {
  const FormatConfig saved{
      .cameraId = "by-id/usb-gone-video-index0", .cameraName = "USB Camera", .mode = k360};
  const FormatChoice choice({laptop()}, saved);

  REQUIRE(choice.cameras().size() == 2);
  CHECK(cameraLabel(choice.cameras()[1]) == "USB Camera (not connected)");
  CHECK(choice.camera() == 1U);
  CHECK(choice.config() == saved);
  CHECK(choice.status().find("not connected") != std::string::npos);
}

TEST_CASE("выбор формата: камеру без MJPEG выбрать нельзя") {
  FormatChoice choice({laptop(), rawOnly()}, FormatConfig{});
  CHECK(cameraLabel(choice.cameras()[1]) == "Raw Cam (no MJPEG)");

  CHECK_FALSE(choice.chooseCamera(1));
  CHECK(choice.camera() == 0U);
  CHECK(choice.status().find("Raw Cam") != std::string::npos);

  CHECK(choice.chooseCamera(0));
  choice.chooseMode(1);
  CHECK(choice.config().mode == k360);
  CHECK(choice.status().empty());
}

TEST_CASE("выбор формата: камер нет — конфигурация без камеры") {
  const FormatChoice choice({}, FormatConfig{});
  CHECK_FALSE(choice.camera());
  CHECK(choice.config().cameraId.empty());
  CHECK(choice.status() == "No cameras found.");
}

TEST_CASE("выбор формата: захвата для ОС нет — строка говорит об этом, а не «камер нет»") {
  const std::string unsupported = "Camera capture on Windows is not supported yet.";

  const FormatChoice empty({}, FormatConfig{}, unsupported);
  CHECK(empty.status() == unsupported);

  // Проект с камерой, записанный на Linux, открыт там, где захвата нет.
  const FormatConfig saved{.cameraId = laptop().id, .cameraName = laptop().name, .mode = k720};
  const FormatChoice remembered({}, saved, unsupported);
  CHECK(remembered.config() == saved);
  CHECK(remembered.status() == unsupported);
}

TEST_CASE("выбор формата: подписи режимов") {
  CHECK(modeLabel(k720) == "1280x720, 30 fps");
  CHECK(
      modeLabel(
          {.width = 1920, .height = 1080, .rateNumerator = 30000, .rateDenominator = 1001}) ==
      "1920x1080, 29.97 fps");
}
