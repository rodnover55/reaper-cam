// cam_probe — живые проверки захвата без REAPER (задачи раздела 5 tasks.md).
//
//   cam_probe list
//       камеры системы, их имена и режимы MJPEG;
//   cam_probe capture <признак> <ширина>x<высота>@<частота> <секунд> <файл.mov>
//       захват в файл: сколько кадров пришло и совпадают ли байты кадров в
//       файле с полученными от камеры.

#include "cam/capture_v4l2/v4l2.hpp"
#include "cam/container/writer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using cam::capture::CameraMode;

int list() {
  cam::capture_v4l2::V4l2Backend backend;
  const auto cameras = backend.list();

  std::printf("cameras: %zu\n", cameras.size());
  for (const auto &camera : cameras) {
    std::printf("%s\n  id: %s\n  modes: %zu%s\n", camera.name.c_str(), camera.id.c_str(),
                camera.modes.size(), camera.modes.empty() ? " (no MJPEG)" : "");
    for (const CameraMode &mode : camera.modes)
      std::printf("    %dx%d @ %d/%d\n", mode.width, mode.height, mode.rateNumerator,
                  mode.rateDenominator);
  }

  return 0;
}

bool parseMode(const std::string &text, CameraMode &mode) {
  int width = 0;
  int height = 0;
  int numerator = 0;
  int denominator = 1;
  if (std::sscanf(text.c_str(), "%dx%d@%d/%d", &width, &height, &numerator, &denominator) < 3)
    return false;

  mode = CameraMode{.width = width,
                    .height = height,
                    .rateNumerator = numerator,
                    .rateDenominator = denominator};
  return true;
}

int capture(const std::string &id, const std::string &modeText, double seconds,
            const std::string &path) {
  CameraMode mode;
  if (!parseMode(modeText, mode)) {
    std::fprintf(stderr, "режим: <ширина>x<высота>@<числитель>[/<знаменатель>]\n");
    return 2;
  }

  cam::capture_v4l2::V4l2Backend backend;
  auto source = backend.open(id, mode);

  auto writer = cam::container::createWriter(
      cam::container::openFileOutput(path),
      cam::container::VideoFormat{.width = mode.width,
                                  .height = mode.height,
                                  .rateNumerator = mode.rateNumerator,
                                  .rateDenominator = mode.rateDenominator});

  std::vector<std::vector<std::uint8_t>> frames;
  std::vector<double> times;
  std::vector<std::uint64_t> sequences;
  std::vector<double> earlier; // насколько время от камеры раньше метки буфера
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);

  while (std::chrono::steady_clock::now() < end) {
    auto frame = source->next(std::chrono::milliseconds(1000));
    if (!frame)
      continue;

    writer->append(frame->jpeg);
    times.push_back(frame->systemTime);
    sequences.push_back(frame->sequence);
    if (frame->timeFromCamera)
      earlier.push_back(frame->systemTime - frame->captureTime);
    frames.push_back(std::move(frame->jpeg));
  }

  writer->finish();

  std::ifstream file(path, std::ios::binary);
  const std::vector<std::uint8_t> written((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());

  std::size_t identical = 0;
  for (const auto &frame : frames)
    if (!std::ranges::search(written, frame).empty())
      ++identical;

  const double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

  std::printf("frames: %zu in %.1f s (%.2f per second)\n", frames.size(), seconds,
              static_cast<double>(frames.size()) / seconds);
  std::printf("identical in file: %zu of %zu\n", identical, frames.size());
  if (!times.empty()) {
    std::printf("buffer timestamps: first %.6f last %.6f, now %.6f (same clock as steady_clock)\n",
                times.front(), times.back(), now);

    // Пропуски по номерам кадров драйвера: потеряно по дороге или камера так
    // и снимала.
    std::uint64_t skipped = 0;
    for (std::size_t i = 1; i < sequences.size(); ++i)
      skipped += sequences[i] - sequences[i - 1] - 1;

    const double span = times.back() - times.front();
    std::printf("sequence %llu..%llu, skipped %llu; camera rate %.2f per second\n",
                static_cast<unsigned long long>(sequences.front()),
                static_cast<unsigned long long>(sequences.back()),
                static_cast<unsigned long long>(skipped),
                span > 0 ? static_cast<double>(times.size() - 1) / span : 0.0);
  }

  std::printf("%s\n", source->describe().c_str());
  if (!earlier.empty()) {
    double mean = 0.0;
    for (const double e : earlier)
      mean += e;
    mean /= static_cast<double>(earlier.size());
    double spread = 0.0;
    for (const double e : earlier)
      spread += (e - mean) * (e - mean);
    spread = std::sqrt(spread / static_cast<double>(earlier.size()));
    const auto [low, high] = std::ranges::minmax(earlier);
    std::printf("camera time earlier than buffer timestamp: mean %.2f ms, sd %.2f ms, "
                "min %.2f ms, max %.2f ms (%zu frames)\n",
                mean * 1e3, spread * 1e3, low * 1e3, high * 1e3, earlier.size());
  }

  return identical == frames.size() && !frames.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  try {
    if (args.size() == 1 && args[0] == "list")
      return list();
    if (args.size() == 5 && args[0] == "capture")
      return capture(args[1], args[2], std::stod(args[3]), args[4]);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }

  std::fprintf(stderr, "usage: cam_probe list | capture <id> <WxH@num/den> <seconds> <file.mov>\n");
  return 2;
}
