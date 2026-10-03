// Захват Windows: Media Foundation, кадры MJPEG как есть.
//
// Источник камеры читается через Source Reader с отключёнными
// преобразователями форматов: кадр приходит таким, каким его отдал драйвер, и
// не разжимается. Чтение асинхронное: Media Foundation зовёт обратный вызов
// из своего потока, тот кладёт кадр в очередь и просит следующий, а поток
// захвата расширения ждёт очередь с таймаутом. Синхронное чтение ждало бы
// кадра сколько угодно и не дало бы заметить молчание камеры.
//
// Время кадра. Драйвер ставит кадру метку системного времени съёмки
// (MFSampleExtension_DeviceReferenceSystemTime, сотни наносекунд по QPC). Она
// переводится в часы расширения через MFGetSystemTime, прочитанное при
// приходе кадра (backend_support.hpp). Нет метки или она сбита — время
// прихода.

#include "cam/capture_mf/media_foundation.hpp"

#include "cam/capture/backend_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace cam::capture_mf {
namespace {

using capture::CameraInfo;
using capture::CameraMode;
using capture::CaptureError;
using capture::Frame;
using Microsoft::WRL::ComPtr;

// Метка времени съёмки от драйвера. Объявлена не во всех версиях заголовков
// Windows SDK, поэтому здесь своя копия.
// {6523775A-BA2D-405F-B2C5-01FF88E2E8F6}
constexpr GUID kDeviceReferenceSystemTime = {
    0x6523775a, 0xba2d, 0x405f, {0xb2, 0xc5, 0x01, 0xff, 0x88, 0xe2, 0xe8, 0xf6}};

/// Сколько кадров ждёт поток захвата. Он забирает их сразу, так что очередь
/// нужна только на случай, если его ненадолго не пустили к процессору.
constexpr std::size_t kQueuedFrames = 8;

/// Сколько деструктор ждёт, пока Media Foundation вернёт уже запрошенный кадр.
constexpr std::chrono::seconds kStopWait{1};

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

/// COM в потоке, пока объект жив. Если поток уже вошёл в COM в другой модели
/// (главный поток REAPER), он так и остаётся: Media Foundation работает в обеих.
class ComScope {
public:
  ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComScope() {
    if (SUCCEEDED(result_))
      CoUninitialize();
  }

  ComScope(const ComScope &) = delete;
  ComScope &operator=(const ComScope &) = delete;
  ComScope(ComScope &&) = delete;
  ComScope &operator=(ComScope &&) = delete;

private:
  HRESULT result_;
};

double steadySeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string utf8(std::wstring_view text) {
  if (text.empty())
    return {};

  const int length = static_cast<int>(text.size());
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
  if (size <= 0)
    return {};

  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring wide(std::string_view text) {
  if (text.empty())
    return {};

  const int length = static_cast<int>(text.size());
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
  if (size <= 0)
    return {};

  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), size);
  return result;
}

/// Причина отказа коротко, как у захвата других ОС: её видно в консоли в
/// скобках у дорожки.
std::string reasonOf(HRESULT result) {
  switch (result) {
  case E_ACCESSDENIED:
    return "access denied: allow camera access for desktop apps in Windows privacy settings";
  case MF_E_HW_MFT_FAILED_START_STREAMING:
  case MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED:
    return "busy";
  case MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED:
    return "not connected";
  default:
    return std::format("device error 0x{:08X}", static_cast<std::uint32_t>(result));
  }
}

std::optional<std::string> stringOf(IMFAttributes *attributes, const GUID &key) {
  wchar_t *value = nullptr;
  UINT32 length = 0;
  if (FAILED(attributes->GetAllocatedString(key, &value, &length)))
    return std::nullopt;

  std::string result = utf8(std::wstring_view(value, length));
  CoTaskMemFree(value);
  return result;
}

/// Камеры видеозахвата системы.
std::vector<ComPtr<IMFActivate>> videoDevices() {
  std::vector<ComPtr<IMFActivate>> devices;

  ComPtr<IMFAttributes> attributes;
  if (FAILED(MFCreateAttributes(&attributes, 1)) ||
      FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    return devices;

  IMFActivate **items = nullptr;
  UINT32 count = 0;
  if (FAILED(MFEnumDeviceSources(attributes.Get(), &items, &count)))
    return devices;

  for (UINT32 index = 0; index < count; ++index) {
    ComPtr<IMFActivate> device;
    device.Attach(items[index]);
    devices.push_back(std::move(device));
  }
  CoTaskMemFree(static_cast<void *>(items));
  return devices;
}

/// Режим MJPEG типа видео; пусто, если тип не MJPEG.
std::optional<CameraMode> mjpegModeOf(IMFMediaType *type) {
  GUID major{};
  GUID subtype{};
  if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video ||
      FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_MJPG)
    return std::nullopt;

  UINT32 width = 0;
  UINT32 height = 0;
  UINT32 numerator = 0;
  UINT32 denominator = 0;
  if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
      FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator)) ||
      width == 0 || height == 0 || numerator == 0 || denominator == 0)
    return std::nullopt;

  return capture::modeOf(static_cast<int>(width), static_cast<int>(height), numerator,
                         denominator);
}

/// Собственные типы видеопотока камеры в MJPEG, с режимами.
std::vector<std::pair<CameraMode, ComPtr<IMFMediaType>>>
mjpegTypesOf(IMFSourceReader *reader) {
  std::vector<std::pair<CameraMode, ComPtr<IMFMediaType>>> types;
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> type;
    if (FAILED(reader->GetNativeMediaType(kVideoStream, index, &type)))
      break;

    if (const auto mode = mjpegModeOf(type.Get()))
      types.emplace_back(*mode, std::move(type));
  }
  return types;
}

/// Источник камеры; останавливается вместе с объектом.
class Source {
public:
  Source() = default;
  explicit Source(ComPtr<IMFMediaSource> source) : source_(std::move(source)) {}

  ~Source() {
    if (source_)
      source_->Shutdown();
  }

  Source(const Source &) = delete;
  Source &operator=(const Source &) = delete;
  Source(Source &&) = delete;
  Source &operator=(Source &&) = delete;

  IMFMediaSource *get() const { return source_.Get(); }

private:
  ComPtr<IMFMediaSource> source_;
};

ComPtr<IMFMediaSource> createSource(const std::wstring &link) {
  ComPtr<IMFAttributes> attributes;
  HRESULT result = MFCreateAttributes(&attributes, 2);
  if (SUCCEEDED(result))
    result = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (SUCCEEDED(result))
    result = attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                   link.c_str());

  ComPtr<IMFMediaSource> source;
  if (SUCCEEDED(result))
    result = MFCreateDeviceSource(attributes.Get(), &source);
  if (FAILED(result))
    throw CaptureError(reasonOf(result));

  return source;
}

/// Обратный вызов Source Reader: кадры в очередь и просьба о следующем.
class Reader final : public IMFSourceReaderCallback {
public:
  Reader() = default;

  Reader(const Reader &) = delete;
  Reader &operator=(const Reader &) = delete;
  Reader(Reader &&) = delete;
  Reader &operator=(Reader &&) = delete;

  STDMETHODIMP QueryInterface(REFIID iid, void **object) override {
    if (object == nullptr)
      return E_POINTER;

    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
      *object = static_cast<IMFSourceReaderCallback *>(this);
      AddRef();
      return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG left = --references_;
    if (left == 0)
      delete this;
    return left;
  }

  STDMETHODIMP OnReadSample(HRESULT status, DWORD /*stream*/, DWORD flags,
                            LONGLONG /*timestamp*/, IMFSample *sample) override {
    // Часы читаются первыми: всё остальное — задержка между приходом кадра и
    // этим отсчётом.
    const double arrival = steadySeconds();
    const double stampNow = static_cast<double>(MFGetSystemTime()) / 1e7;

    std::string error;
    std::optional<Frame> frame;
    if (FAILED(status))
      error = reasonOf(status);
    else if ((flags & MF_SOURCE_READERF_ERROR) != 0)
      error = "device error";
    else if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
      error = "not connected";
    else if (sample != nullptr)
      frame = frameOf(sample, arrival, stampNow);

    bool again = false;
    {
      const std::scoped_lock lock(mutex_);
      pending_ = false;
      if (frame) {
        frame->sequence = sequence_++;
        if (frame->timeFromCamera)
          ++timedByDriver_;
        if (frames_.size() >= kQueuedFrames) {
          frames_.pop_front();
          ++dropped_;
        }
        frames_.push_back(std::move(*frame));
      }
      if (!error.empty() && error_.empty())
        error_ = error;

      again = !stopping_ && error_.empty();
      pending_ = again;
    }
    ready_.notify_all();

    if (again)
      request();
    return S_OK;
  }

  STDMETHODIMP OnFlush(DWORD /*stream*/) override { return S_OK; }

  STDMETHODIMP OnEvent(DWORD /*stream*/, IMFMediaEvent * /*event*/) override { return S_OK; }

  /// Первая просьба о кадре. `reader` держит этот объект и живёт дольше
  /// всех его просьб: перед тем как его отпустить, зовут `stop`.
  void start(IMFSourceReader *reader) {
    {
      const std::scoped_lock lock(mutex_);
      reader_ = reader;
      pending_ = true;
    }
    request();

    const std::scoped_lock lock(mutex_);
    if (!error_.empty())
      throw CaptureError(error_);
  }

  /// Больше кадров не просить и дождаться уже запрошенного.
  void stop() {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    ready_.wait_for(lock, kStopWait, [this] { return !pending_; });
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
    return std::format("Media Foundation: frames {}, timed by driver {}, dropped in queue {}",
                       sequence_, timedByDriver_, dropped_);
  }

private:
  ~Reader() = default;

  void request() {
    IMFSourceReader *reader = nullptr;
    {
      const std::scoped_lock lock(mutex_);
      reader = reader_;
    }

    const HRESULT result =
        reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
    if (SUCCEEDED(result))
      return;

    {
      const std::scoped_lock lock(mutex_);
      pending_ = false;
      if (error_.empty())
        error_ = reasonOf(result);
    }
    ready_.notify_all();
  }

  static std::optional<Frame> frameOf(IMFSample *sample, double arrival, double stampNow) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
      return std::nullopt;

    BYTE *data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length)))
      return std::nullopt;

    Frame frame;
    frame.jpeg.assign(data, data + length);
    buffer->Unlock();

    if (frame.jpeg.empty())
      return std::nullopt; // испорченный кадр — как пропущенный

    frame.systemTime = arrival;
    frame.captureTime = arrival;

    UINT64 stamp = 0;
    if (SUCCEEDED(sample->GetUINT64(kDeviceReferenceSystemTime, &stamp))) {
      if (const auto shot = capture::captureTimeFromStamp(arrival, stampNow,
                                                          static_cast<double>(stamp) / 1e7)) {
        frame.captureTime = *shot;
        frame.timeFromCamera = true;
      }
    }

    return frame;
  }

  std::atomic<ULONG> references_{1};

  mutable std::mutex mutex_;
  std::condition_variable ready_;
  IMFSourceReader *reader_ = nullptr;
  bool pending_ = false;
  bool stopping_ = false;
  std::string error_;
  std::deque<Frame> frames_;
  std::uint64_t sequence_ = 0;
  std::uint64_t timedByDriver_ = 0;
  std::uint64_t dropped_ = 0;
};

class MfCapture : public capture::Capture {
public:
  MfCapture(const std::wstring &link, const CameraMode &mode) : source_(createSource(link)) {
    reader_.Attach(new Reader());

    ComPtr<IMFAttributes> attributes;
    HRESULT result = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(result))
      result = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, reader_.Get());
    if (SUCCEEDED(result))
      result = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    if (SUCCEEDED(result))
      result =
          MFCreateSourceReaderFromMediaSource(source_.get(), attributes.Get(), &sourceReader_);
    if (FAILED(result))
      throw CaptureError(reasonOf(result));

    select(mode);
    reader_->start(sourceReader_.Get());
  }

  ~MfCapture() override {
    reader_->stop();
    sourceReader_.Reset();
  }

  MfCapture(const MfCapture &) = delete;
  MfCapture &operator=(const MfCapture &) = delete;
  MfCapture(MfCapture &&) = delete;
  MfCapture &operator=(MfCapture &&) = delete;

  std::optional<Frame> next(std::chrono::milliseconds timeout) override {
    return reader_->next(timeout);
  }

  std::string describe() const override { return reader_->describe(); }

private:
  void select(const CameraMode &mode) {
    (void)sourceReader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),
                                            FALSE);

    for (const auto &[offered, type] : mjpegTypesOf(sourceReader_.Get())) {
      if (offered != mode)
        continue;

      if (FAILED(sourceReader_->SetStreamSelection(kVideoStream, TRUE)))
        break;

      const HRESULT result =
          sourceReader_->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
      if (SUCCEEDED(result))
        return;
      if (result == MF_E_HW_MFT_FAILED_START_STREAMING || result == E_ACCESSDENIED)
        throw CaptureError(reasonOf(result));
    }

    throw CaptureError("mode not accepted");
  }

  // Порядок важен: COM нужен всем остальным и уходит последним, источник
  // останавливается после того, как отпущен Source Reader.
  ComScope com_;
  Source source_;
  ComPtr<Reader> reader_;
  ComPtr<IMFSourceReader> sourceReader_;
};

} // namespace

MediaFoundationBackend::MediaFoundationBackend()
    : started_(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {}

MediaFoundationBackend::~MediaFoundationBackend() {
  if (started_)
    MFShutdown();
}

std::vector<CameraInfo> MediaFoundationBackend::list() {
  std::vector<CameraInfo> cameras;
  if (!started_)
    return cameras;

  const ComScope com;
  for (const ComPtr<IMFActivate> &device : videoDevices()) {
    auto link =
        stringOf(device.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
    if (!link || link->empty())
      continue;

    CameraInfo camera;
    camera.id = std::move(*link);
    camera.name =
        stringOf(device.Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME).value_or(camera.id);

    ComPtr<IMFMediaSource> object;
    if (SUCCEEDED(device->ActivateObject(IID_PPV_ARGS(&object)))) {
      const Source source(object);
      ComPtr<IMFSourceReader> reader;
      if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(source.get(), nullptr, &reader))) {
        for (const auto &[mode, type] : mjpegTypesOf(reader.Get()))
          camera.modes.push_back(mode);
      }
    }
    (void)device->ShutdownObject();
    capture::sortModes(camera.modes);

    // Камеру, которую уже держит захват, система может не дать открыть
    // второй раз: тогда её режимы — те, что были при прошлом перечне.
    const std::scoped_lock lock(knownModesMutex_);
    if (camera.modes.empty()) {
      if (const auto known = knownModes_.find(camera.id); known != knownModes_.end())
        camera.modes = known->second;
    } else {
      knownModes_[camera.id] = camera.modes;
    }

    cameras.push_back(std::move(camera));
  }

  return cameras;
}

std::unique_ptr<capture::Capture> MediaFoundationBackend::open(const std::string &id,
                                                               const CameraMode &mode) {
  if (!started_)
    throw CaptureError("Media Foundation is not available");

  // Признак проверяется по перечню: так отключённая камера даёт ту же
  // короткую причину, что и на других ОС, а не код ошибки.
  {
    const ComScope com;
    bool connected = false;
    for (const ComPtr<IMFActivate> &device : videoDevices())
      connected =
          connected || stringOf(device.Get(),
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == id;
    if (!connected)
      throw CaptureError("not connected");
  }

  return std::make_unique<MfCapture>(wide(id), mode);
}

std::string MediaFoundationBackend::unsupported() const {
  if (started_)
    return {};

  return "Camera capture on Windows needs Media Foundation, and it did not start "
         "(Windows N editions need the Media Feature Pack).";
}

} // namespace cam::capture_mf
