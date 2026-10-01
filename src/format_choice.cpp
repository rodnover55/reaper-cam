#include "format_choice.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace cam::reaper {

FormatChoice::FormatChoice(std::vector<capture::CameraInfo> cameras, FormatConfig saved,
                           std::string unsupported)
    : unsupported_(std::move(unsupported)) {
  for (auto &info : cameras)
    cameras_.push_back(Camera{.info = std::move(info), .connected = true});

  if (!saved.cameraId.empty()) {
    auto found = std::ranges::find_if(
        cameras_, [&](const Camera &camera) { return camera.info.id == saved.cameraId; });
    if (found == cameras_.end()) {
      cameras_.push_back(Camera{.info = capture::CameraInfo{.id = saved.cameraId,
                                                            .name = saved.cameraName,
                                                            .modes = {saved.mode}},
                                .connected = false});
      found = cameras_.end() - 1;
    }

    if (!found->info.modes.empty()) {
      camera_ = static_cast<std::size_t>(found - cameras_.begin());
      const auto &modes = found->info.modes;
      const auto mode = std::ranges::find(modes, saved.mode);
      mode_ = mode != modes.end() ? static_cast<std::size_t>(mode - modes.begin()) : 0;
      return;
    }
  }

  // Камера не сохранена: первая, которую можно записывать.
  const auto first = std::ranges::find_if(
      cameras_, [](const Camera &camera) { return !camera.info.modes.empty(); });
  if (first != cameras_.end()) {
    camera_ = static_cast<std::size_t>(first - cameras_.begin());
    mode_ = 0;
  }
}

bool FormatChoice::chooseCamera(std::size_t index) {
  if (index >= cameras_.size())
    return false;

  const Camera &camera = cameras_[index];
  if (camera.info.modes.empty()) {
    notice_ = camera.info.name + " does not provide MJPEG and cannot be recorded.";
    return false;
  }

  notice_.clear();
  if (camera_ != index) {
    camera_ = index;
    mode_ = 0;
  }
  return true;
}

void FormatChoice::chooseMode(std::size_t index) {
  if (camera_ && index < cameras_[*camera_].info.modes.size())
    mode_ = index;
}

FormatConfig FormatChoice::config() const {
  if (!camera_ || !mode_)
    return {};

  const Camera &camera = cameras_[*camera_];
  return FormatConfig{.cameraId = camera.info.id,
                      .cameraName = camera.info.name,
                      .mode = camera.info.modes[*mode_]};
}

std::string FormatChoice::status() const {
  if (!notice_.empty())
    return notice_;
  // Захвата для ОС нет: «не нашлось» и «не подключена» здесь врут — камеру
  // ищут не там.
  const bool recordable = camera_ && cameras_[*camera_].connected;
  if (!recordable && !unsupported_.empty())
    return unsupported_;
  if (!camera_)
    return cameras_.empty() ? "No cameras found." : "No camera provides MJPEG.";
  if (!cameras_[*camera_].connected)
    return "The camera is not connected. The choice is kept.";
  return {};
}

std::string cameraLabel(const FormatChoice::Camera &camera) {
  if (!camera.connected)
    return camera.info.name + " (not connected)";
  if (camera.info.modes.empty())
    return camera.info.name + " (no MJPEG)";
  return camera.info.name;
}

std::string modeLabel(const capture::CameraMode &mode) {
  const std::string rate = mode.rateDenominator == 1
                               ? std::to_string(mode.rateNumerator)
                               : std::format("{:.2f}", mode.framesPerSecond());
  return std::format("{}x{}, {} fps", mode.width, mode.height, rate);
}

} // namespace cam::reaper
