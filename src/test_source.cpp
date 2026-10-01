#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetPlayPosition

#include "test_source.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include "journal.hpp"

#include "cam/capture/jpeg_encoder.hpp"
#include "cam/capture/test_pattern.hpp"

#include <WDL/lice/lice.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

namespace cam::reaper {

#ifdef CAM_DEBUG_BUILD

namespace {

using capture::CameraInfo;
using capture::CameraMode;
using capture::Frame;

constexpr CameraMode kTestMode{
    .width = 640, .height = 360, .rateNumerator = 30, .rateDenominator = 1};

/// Отказ тестовой камеры по команде скрипта проверки: так проверяется потеря
/// камеры посреди дубля (design.md D11).
std::atomic<bool> testCameraFailing{false};

class TestCapture : public capture::Capture {
public:
  explicit TestCapture(const CameraMode &mode)
      : mode_(mode), pattern_(mode.width, mode.height),
        start_(std::chrono::steady_clock::now()) {}

  std::optional<Frame> next(std::chrono::milliseconds timeout) override {
    if (testCameraFailing.load())
      throw capture::CaptureError("test camera failure");

    // Кадры идут ровно по номинальной частоте от открытия.
    const auto due =
        start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                     std::chrono::duration<double>(static_cast<double>(sequence_) /
                                                   mode_.framesPerSecond()));
    const auto now = std::chrono::steady_clock::now();
    if (due > now + timeout) {
      std::this_thread::sleep_for(timeout);
      return std::nullopt;
    }
    std::this_thread::sleep_until(due);

    // Слышимая позиция в момент съёмки — то, куда кадр должен лечь
    // (recording-sync).
    const double heard = GetPlayPosition();
    const auto shot = std::chrono::steady_clock::now().time_since_epoch();

    Frame frame;
    frame.jpeg =
        pattern_.render(static_cast<double>(sequence_) / mode_.framesPerSecond(), heard);
    frame.captureTime = std::chrono::duration<double>(shot).count();
    frame.systemTime = frame.captureTime;
    journal("shot n={} t={:.6f} heard={:.6f}", sequence_, frame.captureTime, heard);
    frame.sequence = sequence_++;
    return frame;
  }

  std::string describe() const override { return "test pattern: heard position at shot"; }

private:
  CameraMode mode_;
  capture::TestPattern pattern_;
  std::chrono::steady_clock::time_point start_;
  std::uint64_t sequence_ = 0;
};

/// Кадры идут ровно по номинальной частоте от открытия: ждёт срока кадра
/// номер `sequence`. Ложь — срок не наступит за `timeout`.
bool waitForFrame(std::chrono::steady_clock::time_point start, std::uint64_t sequence,
                  const CameraMode &mode, std::chrono::milliseconds timeout) {
  const auto due = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(static_cast<double>(sequence) /
                                                             mode.framesPerSecond()));
  if (due > std::chrono::steady_clock::now() + timeout) {
    std::this_thread::sleep_for(timeout);
    return false;
  }
  std::this_thread::sleep_until(due);
  return true;
}

// --- Камеры для снимков документации ---------------------------------------
//
// С переменной окружения REAPER_CAM_DEMO_CAMERAS расширение видит только
// вымышленные камеры: имена и режимы как у обычных веб-камер, а кадр —
// нарисованная сцена. Настоящие камеры в этом режиме не перечисляются и не
// открываются: на снимки не попадает то, что видит камера.

constexpr const char *kDemoCameraId = "by-id/usb-Demo_USB_Camera-video-index0";
constexpr const char *kDemoOldCameraId = "by-id/usb-Demo_Old_Camera-video-index0";

LICE_pixel rgb(unsigned red, unsigned green, unsigned blue) {
  return LICE_RGBA(red, green, blue, 255U);
}

/// Круг строками: в собранной части LICE кругов нет.
void fillCircle(LICE_IBitmap *bitmap, float centerX, float centerY, float radius,
                LICE_pixel color) {
  const auto top = static_cast<int>(std::floor(centerY - radius));
  const auto bottom = static_cast<int>(std::ceil(centerY + radius));
  for (int y = top; y <= bottom; ++y) {
    const float dy = (static_cast<float>(y) + 0.5F) - centerY;
    const float squared = (radius * radius) - (dy * dy);
    if (squared <= 0.0F)
      continue;
    const float half = std::sqrt(squared);
    const auto left = static_cast<int>(std::lround(centerX - half));
    const auto right = static_cast<int>(std::lround(centerX + half));
    LICE_FillRect(bitmap, left, y, right - left, 1, color, 1.0F, LICE_BLIT_MODE_COPY);
  }
}

std::vector<std::uint8_t> rgbOf(LICE_IBitmap &bitmap) {
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  const int span = bitmap.getRowSpan();
  const LICE_pixel *bits = bitmap.getBits();

  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
  for (int y = 0; y < height; ++y) {
    const LICE_pixel *row = bits + (static_cast<std::ptrdiff_t>(y) * span);
    for (int x = 0; x < width; ++x) {
      out.push_back(static_cast<std::uint8_t>(LICE_GETR(row[x])));
      out.push_back(static_cast<std::uint8_t>(LICE_GETG(row[x])));
      out.push_back(static_cast<std::uint8_t>(LICE_GETB(row[x])));
    }
  }
  return out;
}

/// Сцена: небо, солнце, холмы и мяч, который катится слева направо за 4 с.
std::vector<std::uint8_t> renderDemoScene(int width, int height, double seconds) {
  LICE_MemBitmap frame(width, height);
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const int horizon = height * 62 / 100;

  const auto sky = static_cast<float>(horizon);
  LICE_GradRect(&frame, 0, 0, width, horizon, 0.36F, 0.62F, 0.90F, 1.0F, 0.0F, 0.0F, 0.0F,
                0.0F, 0.44F / sky, 0.26F / sky, 0.06F / sky, 0.0F, LICE_BLIT_MODE_COPY);
  LICE_FillRect(&frame, 0, horizon, width, height - horizon, rgb(96, 150, 80), 1.0F,
                LICE_BLIT_MODE_COPY);
  fillCircle(&frame, w * 0.82F, h * 0.20F, h * 0.09F, rgb(255, 214, 102));
  fillCircle(&frame, w * 0.22F, h * 0.86F, h * 0.40F, rgb(122, 176, 98));
  fillCircle(&frame, w * 0.78F, h * 0.95F, h * 0.45F, rgb(108, 164, 88));

  const auto phase = static_cast<float>(std::fmod(seconds, 4.0) / 4.0);
  fillCircle(&frame, w * (0.08F + (0.84F * phase)), h * 0.84F, h * 0.06F, rgb(214, 72, 64));

  constexpr int kQuality = 85;
  return capture::compressJpeg(rgbOf(frame), width, height, kQuality);
}

class DemoCapture : public capture::Capture {
public:
  explicit DemoCapture(const CameraMode &mode)
      : mode_(mode), start_(std::chrono::steady_clock::now()) {}

  std::optional<Frame> next(std::chrono::milliseconds timeout) override {
    if (!waitForFrame(start_, sequence_, mode_, timeout))
      return std::nullopt;

    Frame frame;
    frame.jpeg = renderDemoScene(mode_.width, mode_.height,
                                 static_cast<double>(sequence_) / mode_.framesPerSecond());
    frame.captureTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    frame.systemTime = frame.captureTime;
    frame.sequence = sequence_++;
    return frame;
  }

  std::string describe() const override { return "demo camera: drawn scene"; }

private:
  CameraMode mode_;
  std::chrono::steady_clock::time_point start_;
  std::uint64_t sequence_ = 0;
};

class DemoBackend : public capture::Backend {
public:
  std::vector<CameraInfo> list() override {
    auto mode = [](int width, int height) {
      return CameraMode{
          .width = width, .height = height, .rateNumerator = 30, .rateDenominator = 1};
    };
    return {{.id = kDemoCameraId,
             .name = "USB Camera",
             .modes = {mode(1920, 1080), mode(1280, 720), mode(960, 540), mode(640, 480),
                       mode(640, 360)}},
            {.id = kDemoOldCameraId, .name = "Old USB Camera", .modes = {}}};
  }

  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const CameraMode &mode) override {
    if (id != kDemoCameraId)
      throw capture::CaptureError("not connected");
    return std::make_unique<DemoCapture>(mode);
  }
};

class WithTestSource : public capture::Backend {
public:
  explicit WithTestSource(std::unique_ptr<capture::Backend> inner)
      : inner_(std::move(inner)) {}

  std::vector<CameraInfo> list() override {
    std::vector<CameraInfo> cameras = inner_->list();
    cameras.push_back(
        {.id = kTestCameraId, .name = "Test pattern (debug)", .modes = {kTestMode}});
    return cameras;
  }

  std::unique_ptr<capture::Capture> open(const std::string &id,
                                         const CameraMode &mode) override {
    if (id == kTestCameraId)
      return std::make_unique<TestCapture>(mode.width > 0 ? mode : kTestMode);
    return inner_->open(id, mode);
  }

  std::string unsupported() const override { return inner_->unsupported(); }

private:
  std::unique_ptr<capture::Backend> inner_;
};

} // namespace

void failTestCamera(bool failing) {
  testCameraFailing.store(failing);
  journal("test camera failing={}", failing);
}

std::unique_ptr<capture::Backend> withTestSource(std::unique_ptr<capture::Backend> inner) {
  // Читается один раз при загрузке, до потоков расширения.
  if (std::getenv("REAPER_CAM_DEMO_CAMERAS") != nullptr) { // NOLINT(concurrency-mt-unsafe)
    journal("demo cameras instead of system cameras");
    return std::make_unique<DemoBackend>();
  }
  return std::make_unique<WithTestSource>(std::move(inner));
}

#else

std::unique_ptr<capture::Backend> withTestSource(std::unique_ptr<capture::Backend> inner) {
  return inner;
}

void failTestCamera(bool /*failing*/) {}

#endif

} // namespace cam::reaper
