// Перечень камер и разрешение устойчивого признака в узел устройства.

#include "cam/capture_v4l2/v4l2.hpp"

#include "device.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>
#include <set>
#include <system_error>
#include <tuple>

#include <linux/videodev2.h>

namespace cam::capture_v4l2 {
namespace {

namespace fs = std::filesystem;

/// Цель ссылки как путь узла: ссылки в /dev/v4l относительные.
std::optional<fs::path> targetOf(const fs::path &link) {
  std::error_code error;
  const fs::path target = fs::read_symlink(link, error);
  if (error)
    return std::nullopt;

  return (target.is_absolute() ? target : link.parent_path() / target).lexically_normal();
}

/// Возможности узла: захват видео, метаданные или что-то ещё.
struct Capabilities {
  bool videoCapture = false;
  std::string card;
};

std::optional<Capabilities> capabilitiesOf(const fs::path &node) {
  const detail::Device device(node.string());
  if (!device.isOpen())
    return std::nullopt;

  v4l2_capability capability{};
  if (device.control(VIDIOC_QUERYCAP, &capability) != 0)
    return std::nullopt;

  const std::uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
                                 ? capability.device_caps
                                 : capability.capabilities;

  Capabilities result;
  result.videoCapture = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
  result.card.assign(
      reinterpret_cast<const char *>(capability.card),
      strnlen(reinterpret_cast<const char *>(capability.card), sizeof(capability.card)));
  return result;
}

/// Имя камеры, которое сообщает система. У камер USB это строка продукта из
/// дескриптора: V4L2 отдаёт её обрезанной до 32 байт и с именем узла камеры
/// («Integrated_Webcam_HD: Integrate»).
std::string nameOf(const fs::path &node, const std::string &card) {
  const fs::path product =
      fs::path("/sys/class/video4linux") / node.filename() / "device" / ".." / "product";

  std::ifstream file(product);
  std::string name;
  if (file && std::getline(file, name) && !name.empty())
    return name;

  const auto colon = card.find(": ");
  return colon == std::string::npos ? card : card.substr(0, colon);
}

capture::CameraMode modeOf(std::uint32_t width, std::uint32_t height, v4l2_fract interval) {
  // Интервал — секунды на кадр; частота — обратная ему дробь.
  const std::uint32_t divisor = std::gcd(interval.numerator, interval.denominator);
  return capture::CameraMode{.width = static_cast<int>(width),
                             .height = static_cast<int>(height),
                             .rateNumerator = static_cast<int>(interval.denominator / divisor),
                             .rateDenominator =
                                 static_cast<int>(interval.numerator / divisor)};
}

std::vector<capture::CameraMode> mjpegModesOf(const fs::path &node) {
  std::vector<capture::CameraMode> modes;
  const detail::Device device(node.string());
  if (!device.isOpen())
    return modes;

  v4l2_frmsizeenum size{};
  size.pixel_format = V4L2_PIX_FMT_MJPEG;

  for (size.index = 0; device.control(VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
    if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE)
      continue; // у камер UVC размеры всегда перечислены по одному

    v4l2_frmivalenum interval{};
    interval.pixel_format = V4L2_PIX_FMT_MJPEG;
    interval.width = size.discrete.width;
    interval.height = size.discrete.height;

    for (interval.index = 0; device.control(VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0;
         ++interval.index) {
      if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE && interval.discrete.numerator != 0)
        modes.push_back(modeOf(size.discrete.width, size.discrete.height, interval.discrete));
    }
  }

  // Крупные и частые — первыми.
  std::ranges::sort(modes, [](const capture::CameraMode &a, const capture::CameraMode &b) {
    return std::tuple(a.width * a.height, a.framesPerSecond()) >
           std::tuple(b.width * b.height, b.framesPerSecond());
  });
  const auto duplicates = std::ranges::unique(modes);
  modes.erase(duplicates.begin(), duplicates.end());
  return modes;
}

std::vector<fs::path> linksIn(const fs::path &directory) {
  std::vector<fs::path> links;
  std::error_code error;
  for (const auto &entry : fs::directory_iterator(directory, error))
    if (entry.is_symlink())
      links.push_back(entry.path());

  std::ranges::sort(links);
  return links;
}

} // namespace

std::optional<fs::path> resolveCamera(const std::string &id, const fs::path &linkRoot) {
  // Признак — только имя ссылки в одном из двух каталогов: чужой путь не
  // разрешается.
  const fs::path relative(id);
  const auto kind = relative.begin() == relative.end() ? fs::path() : *relative.begin();
  if (kind != "by-id" && kind != "by-path")
    return std::nullopt;

  if (std::distance(relative.begin(), relative.end()) != 2)
    return std::nullopt;

  return targetOf(linkRoot / relative);
}

V4l2Backend::V4l2Backend(std::filesystem::path linkRoot) : linkRoot_(std::move(linkRoot)) {}

std::vector<capture::CameraInfo> V4l2Backend::list() {
  std::vector<capture::CameraInfo> cameras;
  std::set<fs::path> seen;

  // Сначала by-id: у USB-камер в имени ссылки серийный номер, и признак
  // переживает перестановку в другой порт. by-path — для тех, у кого by-id нет.
  for (const char *kind : {"by-id", "by-path"}) {
    for (const fs::path &link : linksIn(linkRoot_ / kind)) {
      const auto node = targetOf(link);
      if (!node || seen.contains(*node))
        continue;

      const auto capabilities = capabilitiesOf(*node);
      if (!capabilities || !capabilities->videoCapture)
        continue; // узел метаданных и прочее служебное

      seen.insert(*node);
      cameras.push_back(
          capture::CameraInfo{.id = std::string(kind) + "/" + link.filename().string(),
                              .name = nameOf(*node, capabilities->card),
                              .modes = mjpegModesOf(*node)});
    }
  }

  return cameras;
}

} // namespace cam::capture_v4l2
