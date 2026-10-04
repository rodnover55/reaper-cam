// Захват V4L2: потоковый режим с отображением буферов в память, MJPEG в
// выбранном режиме, монопольно (design.md D6).
//
// Рядом с видео, если у камеры есть узел метаданных UVC, идёт второй поток:
// заголовки пакетов с часами камеры. По ним время кадра — начало съёмки, а не
// момент прихода буфера (uvc_clock.hpp).

#include "cam/capture_v4l2/v4l2.hpp"

#include "cam/capture_v4l2/uvc_clock.hpp"
#include "device.hpp"

#include <cstring>
#include <fmt/format.h>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>

namespace cam::capture_v4l2 {
namespace {

namespace fs = std::filesystem;
using capture::CaptureError;

/// Буферов у драйвера: камера не ждёт, пока писатель заберёт кадр, а
/// задержка на очереди — не больше пары кадров.
constexpr unsigned kBuffers = 4;

/// Время от камеры принимается, только если оно раньше прихода буфера и не
/// дальше этого: иначе связь часов сбилась, и честнее метка буфера.
constexpr double kLongestExposureToArrival = 0.5;

std::string errorText(int error) { return std::generic_category().message(error); }

double secondsOf(const timeval &time) {
  return static_cast<double>(time.tv_sec) + (static_cast<double>(time.tv_usec) / 1e6);
}

/// Буферы одного потока узла, отображённые в память.
class MappedStream {
public:
  MappedStream(const detail::Device &device, v4l2_buf_type type)
      : device_(device), type_(type) {
    try {
      start();
    } catch (...) {
      release();
      throw;
    }
  }

  ~MappedStream() { release(); }

  MappedStream(const MappedStream &) = delete;
  MappedStream &operator=(const MappedStream &) = delete;
  MappedStream(MappedStream &&) = delete;
  MappedStream &operator=(MappedStream &&) = delete;

  /// Готовый буфер или пусто, если готового нет.
  std::optional<v4l2_buffer> dequeue() {
    v4l2_buffer buffer = blank();
    if (const int error = device_.control(VIDIOC_DQBUF, &buffer); error != 0) {
      if (error == EAGAIN)
        return std::nullopt;
      throw CaptureError("device error: " + errorText(error));
    }
    return buffer;
  }

  void requeue(v4l2_buffer &buffer) {
    if (const int error = device_.control(VIDIOC_QBUF, &buffer); error != 0)
      throw CaptureError("device error: " + errorText(error));
  }

  std::span<const std::uint8_t> data(const v4l2_buffer &buffer) const {
    return {static_cast<const std::uint8_t *>(mapped_[buffer.index].start), buffer.bytesused};
  }

private:
  struct Mapped {
    void *start;
    std::size_t length;
  };

  void start() {
    v4l2_requestbuffers request{};
    request.count = kBuffers;
    request.type = type_;
    request.memory = V4L2_MEMORY_MMAP;

    if (const int error = device_.control(VIDIOC_REQBUFS, &request); error != 0)
      throw CaptureError(error == EBUSY ? "busy" : "cannot get buffers: " + errorText(error));

    for (unsigned index = 0; index < request.count; ++index) {
      v4l2_buffer buffer = blank();
      buffer.index = index;

      if (const int error = device_.control(VIDIOC_QUERYBUF, &buffer); error != 0)
        throw CaptureError("cannot get buffers: " + errorText(error));

      void *start = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                           device_.fd(), buffer.m.offset);
      if (start == MAP_FAILED)
        throw CaptureError("cannot map buffers: " + errorText(errno));

      mapped_.push_back({.start = start, .length = buffer.length});

      if (const int error = device_.control(VIDIOC_QBUF, &buffer); error != 0)
        throw CaptureError("cannot queue buffers: " + errorText(error));
    }

    v4l2_buf_type streamType = type_;
    if (const int error = device_.control(VIDIOC_STREAMON, &streamType); error != 0)
      throw CaptureError(error == EBUSY ? "busy" : "cannot start: " + errorText(error));

    streaming_ = true;
  }

  void release() {
    if (streaming_) {
      v4l2_buf_type streamType = type_;
      (void)device_.control(VIDIOC_STREAMOFF, &streamType);
      streaming_ = false;
    }

    for (const Mapped &buffer : mapped_)
      ::munmap(buffer.start, buffer.length);
    mapped_.clear();
  }

  v4l2_buffer blank() const {
    v4l2_buffer buffer{};
    buffer.type = type_;
    buffer.memory = V4L2_MEMORY_MMAP;
    return buffer;
  }

  const detail::Device &device_;
  v4l2_buf_type type_;
  std::vector<Mapped> mapped_;
  bool streaming_ = false;
};

/// Узел метаданных той же камеры: у камер UVC это второй узел того же
/// интерфейса USB.
std::optional<fs::path> metadataNodeOf(const fs::path &videoNode) {
  const fs::path classes = "/sys/class/video4linux";
  std::error_code error;
  const fs::path device = fs::canonical(classes / videoNode.filename() / "device", error);
  if (error)
    return std::nullopt;

  for (const auto &entry : fs::directory_iterator(classes, error)) {
    if (entry.path().filename() == videoNode.filename())
      continue;

    std::error_code otherError;
    if (fs::canonical(entry.path() / "device", otherError) != device || otherError)
      continue;

    fs::path node = fs::path("/dev") / entry.path().filename();
    const detail::Device probe(node.string());
    v4l2_capability capability{};
    if (probe.isOpen() && probe.control(VIDIOC_QUERYCAP, &capability) == 0 &&
        (capability.device_caps & V4L2_CAP_META_CAPTURE) != 0)
      return node;
  }

  return std::nullopt;
}

class V4l2Capture : public capture::Capture {
public:
  V4l2Capture(const fs::path &node, const capture::CameraMode &mode) : device_(node.string()) {
    if (!device_.isOpen())
      throw CaptureError(errno == EBUSY ? "busy" : "cannot open: " + errorText(errno));

    configure(mode);
    video_ = std::make_unique<MappedStream>(device_, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    openMetadata(node);
  }

  V4l2Capture(const V4l2Capture &) = delete;
  V4l2Capture &operator=(const V4l2Capture &) = delete;
  V4l2Capture(V4l2Capture &&) = delete;
  V4l2Capture &operator=(V4l2Capture &&) = delete;
  ~V4l2Capture() override = default;

  std::optional<capture::Frame> next(std::chrono::milliseconds timeout) override {
    pollfd wait{.fd = device_.fd(), .events = POLLIN, .revents = 0};
    const int ready = ::poll(&wait, 1, static_cast<int>(timeout.count()));
    if (ready < 0 && errno != EINTR)
      throw CaptureError("poll: " + errorText(errno));
    if (ready <= 0)
      return std::nullopt;
    if ((wait.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
      throw CaptureError("device error");

    auto buffer = video_->dequeue();
    if (!buffer)
      return std::nullopt;

    // Метаданные кадра драйвер отдаёт вместе с ним: к этому моменту они уже
    // в очереди узла метаданных.
    drainMetadata();

    const bool damaged = (buffer->flags & V4L2_BUF_FLAG_ERROR) != 0 || buffer->bytesused == 0;
    capture::Frame frame;
    if (!damaged) {
      const auto bytes = video_->data(*buffer);
      frame.jpeg.assign(bytes.begin(), bytes.end());
      frame.sequence = buffer->sequence;
      frame.systemTime = secondsOf(buffer->timestamp);
      frame.captureTime = frame.systemTime;
      takeCameraTime(frame);
    }

    video_->requeue(*buffer);

    if (damaged)
      return std::nullopt; // испорченный кадр — как пропущенный

    return frame;
  }

  std::string describe() const override {
    if (!metadata_)
      return "no metadata node: buffer timestamps";

    std::string text = fmt::format(
        "metadata {}: headers {}, with PTS {}, with SCR {}, frames timed by camera {}",
        metadataNode_.string(), headers_, withPts_, withScr_, timedByCamera_);
    if (const auto hz = clock_.deviceClockHz())
      text += fmt::format(", camera clock {:.3f} MHz", *hz / 1e6);
    return text;
  }

private:
  void configure(const capture::CameraMode &mode) {
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<std::uint32_t>(mode.width);
    format.fmt.pix.height = static_cast<std::uint32_t>(mode.height);
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (const int error = device_.control(VIDIOC_S_FMT, &format); error != 0)
      throw CaptureError(error == EBUSY ? "busy" : "mode not accepted: " + errorText(error));

    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG ||
        format.fmt.pix.width != static_cast<std::uint32_t>(mode.width) ||
        format.fmt.pix.height != static_cast<std::uint32_t>(mode.height))
      throw CaptureError("mode not accepted");

    // Интервал кадра — дробь, обратная частоте.
    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parameters.parm.capture.timeperframe.numerator =
        static_cast<std::uint32_t>(mode.rateDenominator);
    parameters.parm.capture.timeperframe.denominator =
        static_cast<std::uint32_t>(mode.rateNumerator);
    (void)device_.control(VIDIOC_S_PARM, &parameters);
  }

  /// Метаданные необязательны: без них кадру остаётся метка буфера.
  void openMetadata(const fs::path &videoNode) {
    const auto node = metadataNodeOf(videoNode);
    if (!node)
      return;

    detail::Device device(node->string());
    if (!device.isOpen())
      return;

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_META_CAPTURE;
    format.fmt.meta.dataformat = V4L2_META_FMT_UVC;
    if (device.control(VIDIOC_S_FMT, &format) != 0 ||
        format.fmt.meta.dataformat != V4L2_META_FMT_UVC)
      return;

    try {
      metadataDevice_ = std::move(device);
      metadata_ = std::make_unique<MappedStream>(metadataDevice_, V4L2_BUF_TYPE_META_CAPTURE);
      metadataNode_ = *node;
    } catch (const CaptureError &) {
      metadata_.reset(); // без метаданных — метки буфера
    }
  }

  void drainMetadata() {
    if (!metadata_)
      return;

    while (auto buffer = metadata_->dequeue()) {
      std::optional<std::uint32_t> pts;
      for (const UvcSample &sample : parseUvcMetadata(metadata_->data(*buffer))) {
        ++headers_;
        if (sample.pts) {
          ++withPts_;
          if (!pts)
            pts = sample.pts;
        }
        if (sample.stc) {
          ++withScr_;
          clock_.add(sample);
        }
      }

      if (pts)
        ptsBySequence_[buffer->sequence] = *pts;

      metadata_->requeue(*buffer);
    }

    // Старые кадры уже не спросят.
    while (ptsBySequence_.size() > std::size_t{2} * kBuffers)
      ptsBySequence_.erase(ptsBySequence_.begin());
  }

  void takeCameraTime(capture::Frame &frame) {
    const auto pts = ptsBySequence_.find(static_cast<std::uint32_t>(frame.sequence));
    if (pts == ptsBySequence_.end())
      return;

    const auto start = clock_.exposureStart(pts->second);
    if (!start || *start > frame.systemTime ||
        frame.systemTime - *start > kLongestExposureToArrival)
      return;

    frame.captureTime = *start;
    frame.timeFromCamera = true;
    ++timedByCamera_;
  }

  detail::Device device_;
  std::unique_ptr<MappedStream> video_;

  detail::Device metadataDevice_;
  std::unique_ptr<MappedStream> metadata_;
  fs::path metadataNode_;
  UvcClock clock_;
  std::map<std::uint32_t, std::uint32_t> ptsBySequence_;
  std::uint64_t headers_ = 0;
  std::uint64_t withPts_ = 0;
  std::uint64_t withScr_ = 0;
  std::uint64_t timedByCamera_ = 0;
};

} // namespace

std::unique_ptr<capture::Capture> V4l2Backend::open(const std::string &id,
                                                    const capture::CameraMode &mode) {
  const auto node = resolveCamera(id, linkRoot_);
  if (!node)
    throw CaptureError("not connected");

  return std::make_unique<V4l2Capture>(*node, mode);
}

} // namespace cam::capture_v4l2
