// Захват macOS: AVFoundation.
//
// Камера с MJPEG ('jpeg' или 'dmb1', обычно камеры USB) открывается в нём, а
// выход данных видео получает пустые настройки: так AVFoundation отдаёт кадры
// в собственном формате устройства — сжатыми, и они пишутся как есть.
//
// Камера без MJPEG — встроенная камера Mac, iPhone как камера — отдаёт кадры
// NV12, и их сжимает в JPEG VideoToolbox (frame_compressor.hpp). Если камера
// с MJPEG всё же прислала картинку, а не сжатые данные, кадр сжимается так же.
//
// Кадры приходят в обратный вызов на своей очереди GCD, тот кладёт их в
// очередь, а поток захвата расширения ждёт её с таймаутом.
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
#include "cam/capture_avf/jpeg_compressor.hpp"
#include "frame_compressor.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fmt/format.h>
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

  void push(Frame frame, bool compressedHere) {
    {
      const std::scoped_lock lock(mutex_);
      frame.sequence = sequence_++;
      if (frame.timeFromCamera)
        ++timedByCamera_;
      if (compressedHere)
        ++compressed_;
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
    return fmt::format("AVFoundation: frames {}, timed by camera {}, late {}, compressed "
                       "here {}, dropped in queue {}",
                       sequence_, timedByCamera_, late_, compressed_, dropped_);
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
  std::uint64_t compressed_ = 0;
  std::uint64_t dropped_ = 0;
};

} // namespace cam::capture_avf::detail

/// Получатель кадров выхода данных видео.
@interface CamAvfFrameReceiver : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithInbox:(std::shared_ptr<cam::capture_avf::detail::Inbox>)inbox
                   compressor:
                       (std::shared_ptr<cam::capture_avf::detail::FrameCompressor>)compressor;
@end

@implementation CamAvfFrameReceiver {
  std::shared_ptr<cam::capture_avf::detail::Inbox> inbox_;
  std::shared_ptr<cam::capture_avf::detail::FrameCompressor> compressor_;
}

- (instancetype)initWithInbox:(std::shared_ptr<cam::capture_avf::detail::Inbox>)inbox
                   compressor:
                       (std::shared_ptr<cam::capture_avf::detail::FrameCompressor>)compressor {
  self = [super init];
  if (self != nil) {
    inbox_ = std::move(inbox);
    compressor_ = std::move(compressor);
  }
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

  cam::capture::Frame frame;
  bool compressedHere = false;

  if (CVImageBufferRef pixels = CMSampleBufferGetImageBuffer(sampleBuffer)) {
    // Картинка, а не сжатые данные: камера без MJPEG.
    auto jpeg = compressor_->compress(pixels);
    if (!jpeg) {
      inbox_->fail("cannot compress frames");
      return;
    }
    frame.jpeg = std::move(*jpeg);
    compressedHere = true;
  } else {
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    const std::size_t length = block != nullptr ? CMBlockBufferGetDataLength(block) : 0;
    if (length == 0)
      return; // испорченный кадр — как пропущенный

    frame.jpeg.resize(length);
    if (CMBlockBufferCopyDataBytes(block, 0, length, frame.jpeg.data()) != kCMBlockBufferNoErr)
      return;
  }

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

  inbox_->push(std::move(frame), compressedHere);
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

/// Режимы всех форматов камеры: MJPEG пишется как есть, остальное сжимается
/// при захвате. У камер USB частоты перечислены по одной (края диапазона
/// совпадают), у встроенных — диапазоном «от 1 до 30».
std::vector<CameraMode> modesOf(AVCaptureDevice *device) {
  std::vector<CameraMode> modes;
  for (AVCaptureDeviceFormat *format in device.formats) {
    const CMVideoDimensions size =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    for (AVFrameRateRange *range in format.videoSupportedFrameRateRanges) {
      // Длительность кадра дробью value/timescale; частота — обратная.
      const CMTime slowest = range.maxFrameDuration;
      const CMTime fastest = range.minFrameDuration;
      for (const CameraMode &mode :
           capture::modesInRateRange(size.width, size.height, slowest.timescale, slowest.value,
                                     fastest.timescale, fastest.value))
        modes.push_back(mode);
    }
  }

  capture::sortModes(modes);
  return modes;
}

NSArray<AVCaptureDevice *> *videoDevices() {
  NSMutableArray<AVCaptureDeviceType> *types =
      [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
  if (@available(macOS 14.0, *)) {
    [types addObject:AVCaptureDeviceTypeExternal];
    [types addObject:AVCaptureDeviceTypeContinuityCamera]; // iPhone как камера
  } else {
    [types addObject:AVCaptureDeviceTypeExternalUnknown];
  }

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
  /// Формат MJPEG: кадры пишутся как есть.
  bool mjpeg = false;
};

/// Длительность кадра режима в диапазоне `range`. Край диапазона берётся
/// как его сообщила система: камеры USB принимают только его.
CMTime durationIn(AVFrameRateRange *range, const CameraMode &mode) {
  for (const CMTime edge : {range.minFrameDuration, range.maxFrameDuration})
    if (capture::sameRate(mode, edge.timescale, edge.value))
      return edge;
  return CMTimeMake(mode.rateDenominator, mode.rateNumerator);
}

/// Формат режима. Если режим есть и в MJPEG, и без сжатия, берётся MJPEG:
/// он пишется без пережатия.
std::optional<Choice> choose(AVCaptureDevice *device, const CameraMode &mode) {
  std::optional<Choice> found;
  for (AVCaptureDeviceFormat *format in device.formats) {
    const CMVideoDimensions size =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    if (size.width != mode.width || size.height != mode.height)
      continue;

    for (AVFrameRateRange *range in format.videoSupportedFrameRateRanges) {
      const CMTime slowest = range.maxFrameDuration;
      const CMTime fastest = range.minFrameDuration;
      if (!capture::rateInRange(mode, slowest.timescale, slowest.value, fastest.timescale,
                                fastest.value))
        continue;

      const Choice choice{
          .format = format, .duration = durationIn(range, mode), .mjpeg = isMjpeg(format)};
      if (choice.mjpeg)
        return choice;
      if (!found)
        found = choice;
    }
  }
  return found;
}

/// Формат кадров, который просит у выхода данных видео захват без MJPEG:
/// NV12 в диапазоне камеры — без лишнего пересчёта.
NSDictionary *pixelSettingsFor(AVCaptureDeviceFormat *format) {
  const FourCharCode native = CMFormatDescriptionGetMediaSubType(format.formatDescription);
  const OSType pixels = native == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                            ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                            : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  return @{(id)kCVPixelBufferPixelFormatTypeKey : @(pixels)};
}

/// Часы, в которых сеанс ставит метки кадрам. До macOS 12.3 они назывались
/// masterClock; модуль грузится и на более старых системах (MacBook Pro 2015
/// доходит до 12.x).
CMClockRef synchronizationClockOf(AVCaptureSession *session) {
  if (@available(macOS 12.3, *))
    return session.synchronizationClock;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return session.masterClock;
#pragma clang diagnostic pop
}

class AvfCapture : public capture::Capture {
public:
  AvfCapture(AVCaptureDevice *device, const CameraMode &mode)
      : inbox_(std::make_shared<detail::Inbox>()),
        compressor_(std::make_shared<detail::FrameCompressor>(kJpegQuality)),
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

  std::string describe() const override {
    return inbox_->describe() + ", " + compressor_->describe();
  }

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
    receiver_ = [[CamAvfFrameReceiver alloc] initWithInbox:inbox_ compressor:compressor_];

    // MJPEG: пустые настройки — собственный формат устройства, сжатые кадры
    // без разжатия. Иначе — NV12 для кодера JPEG.
    output_.videoSettings = choice->mjpeg ? @{} : pixelSettingsFor(choice->format);
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

    inbox_->setClock(synchronizationClockOf(session_));
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
  std::shared_ptr<detail::FrameCompressor> compressor_;
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
      camera.modes = modesOf(device);
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
