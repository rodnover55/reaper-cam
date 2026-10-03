// Захват macOS: AVFoundation, кадры MJPEG как есть.
//
// Камера открывается в формате MJPEG ('jpeg' или 'dmb1'), а выход данных видео
// получает пустые настройки: так AVFoundation отдаёт кадры в собственном
// формате устройства — сжатыми, без разжатия в пиксели. Кадры приходят в
// обратный вызов на своей очереди GCD, тот кладёт их в очередь, а поток
// захвата расширения ждёт её с таймаутом.
//
// Время кадра. Метка кадра — начало съёмки в часах синхронизации сеанса. Она
// переводится в часы хоста (host time), а оттуда — в часы расширения через
// часы хоста, прочитанные при приходе кадра (backend_support.hpp). Метка
// сбита — время прихода.
//
// Разрешение на камеру. Без NSCameraUsageDescription в Info.plist программы
// система закрывает программу при первом обращении к камере, поэтому сначала
// проверяется он, а уже потом — разрешение пользователя.

#include "cam/capture_avf/avfoundation.hpp"

#include "cam/capture/backend_support.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cam::capture_avf::detail {

using capture::CaptureError;
using capture::Frame;

/// Сколько кадров ждёт поток захвата.
constexpr std::size_t kQueuedFrames = 8;

double steadySeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

/// Почтовый ящик между обратным вызовом AVFoundation и потоком захвата.
class Inbox {
public:
  Inbox() = default;
  ~Inbox() { setClock(nullptr); }

  Inbox(const Inbox &) = delete;
  Inbox &operator=(const Inbox &) = delete;
  Inbox(Inbox &&) = delete;
  Inbox &operator=(Inbox &&) = delete;

  void push(Frame frame) {
    {
      const std::scoped_lock lock(mutex_);
      frame.sequence = sequence_++;
      if (frame.timeFromCamera)
        ++timedByCamera_;
      if (frames_.size() >= kQueuedFrames) {
        frames_.pop_front();
        ++dropped_;
      }
      frames_.push_back(std::move(frame));
    }
    ready_.notify_all();
  }

  void fail(const std::string &reason) {
    {
      const std::scoped_lock lock(mutex_);
      if (error_.empty())
        error_ = reason;
    }
    ready_.notify_all();
  }

  /// Кадр пришёл разжатым: камера не отдаёт MJPEG как есть.
  void decoded() {
    {
      const std::scoped_lock lock(mutex_);
      ++decoded_;
    }
    fail("camera delivers decoded frames only");
  }

  void late() {
    const std::scoped_lock lock(mutex_);
    ++late_;
  }

  /// Часы синхронизации сеанса: в них метки кадров.
  void setClock(CMClockRef clock) {
    if (clock != nullptr)
      CFRetain(clock);

    CMClockRef previous = nullptr;
    {
      const std::scoped_lock lock(mutex_);
      previous = std::exchange(clock_, clock);
    }
    if (previous != nullptr)
      CFRelease(previous);
  }

  /// Часы меток с лишней ссылкой: её отпускает вызвавший.
  CMClockRef retainedClock() const {
    const std::scoped_lock lock(mutex_);
    if (clock_ != nullptr)
      CFRetain(clock_);
    return clock_;
  }

  std::optional<Frame> next(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    ready_.wait_for(lock, timeout, [this] { return !frames_.empty() || !error_.empty(); });

    if (!frames_.empty()) {
      Frame frame = std::move(frames_.front());
      frames_.pop_front();
      return frame;
    }

    if (!error_.empty())
      throw CaptureError(error_);

    return std::nullopt;
  }

  std::string describe() const {
    const std::scoped_lock lock(mutex_);
    return std::format("AVFoundation: frames {}, timed by camera {}, late {}, decoded {}, "
                       "dropped in queue {}",
                       sequence_, timedByCamera_, late_, decoded_, dropped_);
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Frame> frames_;
  std::string error_;
  CMClockRef clock_ = nullptr;
  std::uint64_t sequence_ = 0;
  std::uint64_t timedByCamera_ = 0;
  std::uint64_t late_ = 0;
  std::uint64_t decoded_ = 0;
  std::uint64_t dropped_ = 0;
};

} // namespace cam::capture_avf::detail

/// Получатель кадров выхода данных видео.
@interface CamAvfFrameReceiver : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithInbox:(std::shared_ptr<cam::capture_avf::detail::Inbox>)inbox;
@end

@implementation CamAvfFrameReceiver {
  std::shared_ptr<cam::capture_avf::detail::Inbox> inbox_;
}

- (instancetype)initWithInbox:(std::shared_ptr<cam::capture_avf::detail::Inbox>)inbox {
  self = [super init];
  if (self != nil)
    inbox_ = std::move(inbox);
  return self;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  (void)output;
  (void)connection;

  // Часы читаются первыми: всё остальное — задержка между приходом кадра и
  // этим отсчётом.
  const double arrival = cam::capture_avf::detail::steadySeconds();
  CMClockRef host = CMClockGetHostTimeClock();
  const CMTime hostNow = CMClockGetTime(host);

  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
  if (block == nullptr) {
    inbox_->decoded();
    return;
  }

  const std::size_t length = CMBlockBufferGetDataLength(block);
  if (length == 0)
    return; // испорченный кадр — как пропущенный

  cam::capture::Frame frame;
  frame.jpeg.resize(length);
  if (CMBlockBufferCopyDataBytes(block, 0, length, frame.jpeg.data()) != kCMBlockBufferNoErr)
    return;

  frame.systemTime = arrival;
  frame.captureTime = arrival;

  const CMTime stamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
  if (CMTIME_IS_NUMERIC(stamp)) {
    CMClockRef clock = inbox_->retainedClock();
    const CMTime shot = clock != nullptr ? CMSyncConvertTime(stamp, clock, host) : stamp;
    if (clock != nullptr)
      CFRelease(clock);

    if (CMTIME_IS_NUMERIC(shot)) {
      if (const auto start = cam::capture::captureTimeFromStamp(
              arrival, CMTimeGetSeconds(hostNow), CMTimeGetSeconds(shot))) {
        frame.captureTime = *start;
        frame.timeFromCamera = true;
      }
    }
  }

  inbox_->push(std::move(frame));
}

- (void)captureOutput:(AVCaptureOutput *)output
    didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
  (void)output;
  (void)sampleBuffer;
  (void)connection;
  inbox_->late();
}

@end

namespace cam::capture_avf {
namespace {

using capture::CameraInfo;
using capture::CameraMode;
using capture::CaptureError;
using capture::Frame;

std::string utf8(NSString *text) {
  const char *bytes = text != nil ? text.UTF8String : nullptr;
  return bytes != nullptr ? std::string(bytes) : std::string();
}

NSString *nsString(const std::string &text) {
  return [NSString stringWithUTF8String:text.c_str()];
}

bool isMjpeg(AVCaptureDeviceFormat *format) {
  const FourCharCode codec = CMFormatDescriptionGetMediaSubType(format.formatDescription);
  return codec == kCMVideoCodecType_JPEG || codec == kCMVideoCodecType_JPEG_OpenDML;
}

/// Режим формата с длительностью кадра `duration`: частота — обратная ей дробь.
CameraMode formatMode(AVCaptureDeviceFormat *format, CMTime duration) {
  const CMVideoDimensions size =
      CMVideoFormatDescriptionGetDimensions(format.formatDescription);
  return capture::modeOf(size.width, size.height, duration.timescale, duration.value);
}

std::vector<CameraMode> mjpegModesOf(AVCaptureDevice *device) {
  std::vector<CameraMode> modes;
  for (AVCaptureDeviceFormat *format in device.formats) {
    if (!isMjpeg(format))
      continue;

    // У камер USB частоты перечислены по одной: у диапазона края совпадают.
    for (AVFrameRateRange *range in format.videoSupportedFrameRateRanges) {
      modes.push_back(formatMode(format, range.minFrameDuration));
      if (CMTimeCompare(range.minFrameDuration, range.maxFrameDuration) != 0)
        modes.push_back(formatMode(format, range.maxFrameDuration));
    }
  }

  capture::sortModes(modes);
  return modes;
}

NSArray<AVCaptureDevice *> *videoDevices() {
  NSMutableArray<AVCaptureDeviceType> *types =
      [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
  if (@available(macOS 14.0, *))
    [types addObject:AVCaptureDeviceTypeExternal];
  else
    [types addObject:AVCaptureDeviceTypeExternalUnknown];

  AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession
      discoverySessionWithDeviceTypes:types
                            mediaType:AVMediaTypeVideo
                             position:AVCaptureDevicePositionUnspecified];
  return discovery.devices;
}

/// Разрешено ли программе брать камеру; если нет — ошибка устройства с
/// причиной, которую увидит пользователь.
void requireCameraAccess() {
  if ([NSBundle.mainBundle objectForInfoDictionaryKey:@"NSCameraUsageDescription"] == nil)
    throw CaptureError(
        "the application does not declare camera use (NSCameraUsageDescription)");

  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
  case AVAuthorizationStatusAuthorized:
    return;
  case AVAuthorizationStatusNotDetermined:
    // Система спросит пользователя; захват откроется при следующем arm.
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL granted) {
                               (void)granted;
                             }];
    throw CaptureError("waiting for camera permission: allow it and arm the track again");
  case AVAuthorizationStatusDenied:
  case AVAuthorizationStatusRestricted:
    break;
  }
  throw CaptureError(
      "access denied: allow camera access in System Settings, Privacy & Security");
}

/// Формат и длительность кадра режима; пусто, если у камеры такого нет.
struct Choice {
  AVCaptureDeviceFormat *format = nil;
  CMTime duration = kCMTimeInvalid;
};

std::optional<Choice> choose(AVCaptureDevice *device, const CameraMode &mode) {
  for (AVCaptureDeviceFormat *format in device.formats) {
    if (!isMjpeg(format))
      continue;

    const CMVideoDimensions size =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    if (size.width != mode.width || size.height != mode.height)
      continue;

    for (AVFrameRateRange *range in format.videoSupportedFrameRateRanges) {
      for (const CMTime duration : {range.minFrameDuration, range.maxFrameDuration}) {
        if (capture::sameRate(mode, duration.timescale, duration.value))
          return Choice{.format = format, .duration = duration};
      }
    }
  }
  return std::nullopt;
}

class AvfCapture : public capture::Capture {
public:
  AvfCapture(AVCaptureDevice *device, const CameraMode &mode)
      : inbox_(std::make_shared<detail::Inbox>()),
        queue_(dispatch_queue_create("reaper-cam.capture", DISPATCH_QUEUE_SERIAL)) {
    try {
      start(device, mode);
    } catch (...) {
      close();
      throw;
    }
  }

  ~AvfCapture() override {
    @autoreleasepool {
      close();
    }
  }

  AvfCapture(const AvfCapture &) = delete;
  AvfCapture &operator=(const AvfCapture &) = delete;
  AvfCapture(AvfCapture &&) = delete;
  AvfCapture &operator=(AvfCapture &&) = delete;

  std::optional<Frame> next(std::chrono::milliseconds timeout) override {
    return inbox_->next(timeout);
  }

  std::string describe() const override { return inbox_->describe(); }

private:
  void start(AVCaptureDevice *device, const CameraMode &mode) {
    const auto choice = choose(device, mode);
    if (!choice)
      throw CaptureError("mode not accepted");

    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                                        error:&error];
    if (input == nil)
      throw CaptureError("cannot open: " + utf8(error.localizedDescription));

    session_ = [[AVCaptureSession alloc] init];
    output_ = [[AVCaptureVideoDataOutput alloc] init];
    receiver_ = [[CamAvfFrameReceiver alloc] initWithInbox:inbox_];

    // Пустые настройки — собственный формат устройства: MJPEG без разжатия.
    output_.videoSettings = @{};
    output_.alwaysDiscardsLateVideoFrames = NO;
    [output_ setSampleBufferDelegate:receiver_ queue:queue_];

    [session_ beginConfiguration];
    if (![session_ canAddInput:input] || ![session_ canAddOutput:output_]) {
      [session_ commitConfiguration];
      throw CaptureError("busy");
    }
    [session_ addInput:input];
    [session_ addOutput:output_];

    // Формат держится, пока устройство заперто: отпирается после запуска,
    // иначе сеанс вправе выбрать формат сам.
    if (![device lockForConfiguration:&error]) {
      [session_ commitConfiguration];
      throw CaptureError("busy");
    }
    device.activeFormat = choice->format;
    device.activeVideoMinFrameDuration = choice->duration;
    device.activeVideoMaxFrameDuration = choice->duration;
    [session_ commitConfiguration];

    observe(device);
    [session_ startRunning];
    [device unlockForConfiguration];

    if (!session_.running)
      throw CaptureError("cannot start");

    inbox_->setClock(session_.synchronizationClock);
  }

  /// Останавливает то, что успело запуститься. Сообщения нулевым объектам в
  /// Objective-C ничего не делают, поэтому годится и для недостроенного.
  void close() {
    [session_ stopRunning];
    [output_ setSampleBufferDelegate:nil queue:nullptr];
    // Обратный вызов, который уже идёт, доходит до конца.
    dispatch_sync(queue_, ^{
                  });

    for (id observer : observers_)
      [NSNotificationCenter.defaultCenter removeObserver:observer];
    observers_.clear();
  }

  /// Отключение камеры и сбой сеанса — ошибка устройства.
  void observe(AVCaptureDevice *device) {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    std::shared_ptr<detail::Inbox> inbox = inbox_;

    observers_.push_back([center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                                             object:device
                                              queue:nil
                                         usingBlock:^(NSNotification *note) {
                                           (void)note;
                                           inbox->fail("not connected");
                                         }]);

    observers_.push_back([center
        addObserverForName:AVCaptureSessionRuntimeErrorNotification
                    object:session_
                     queue:nil
                usingBlock:^(NSNotification *note) {
                  NSError *error = note.userInfo[AVCaptureSessionErrorKey];
                  inbox->fail("device error: " + utf8(error.localizedDescription));
                }]);
  }

  std::shared_ptr<detail::Inbox> inbox_;
  dispatch_queue_t queue_;
  AVCaptureSession *session_ = nil;
  AVCaptureVideoDataOutput *output_ = nil;
  CamAvfFrameReceiver *receiver_ = nil;
  std::vector<id> observers_;
};

} // namespace

std::vector<CameraInfo> AvFoundationBackend::list() {
  std::vector<CameraInfo> cameras;
  @autoreleasepool {
    for (AVCaptureDevice *device in videoDevices()) {
      CameraInfo camera;
      camera.id = utf8(device.uniqueID);
      camera.name = utf8(device.localizedName);
      camera.modes = mjpegModesOf(device);
      cameras.push_back(std::move(camera));
    }
  }
  return cameras;
}

std::unique_ptr<capture::Capture> AvFoundationBackend::open(const std::string &id,
                                                            const CameraMode &mode) {
  // Исключение не выходит из пула автоосвобождения: пул тогда не опустошается.
  std::unique_ptr<capture::Capture> capture;
  std::string failure;
  @autoreleasepool {
    try {
      AVCaptureDevice *device = [AVCaptureDevice deviceWithUniqueID:nsString(id)];
      if (device == nil)
        throw CaptureError("not connected");

      requireCameraAccess();
      capture = std::make_unique<AvfCapture>(device, mode);
    } catch (const CaptureError &error) {
      failure = error.what();
    }
  }

  if (!capture)
    throw CaptureError(failure);
  return capture;
}

} // namespace cam::capture_avf
