#include "cam/capture/device_registry.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace cam::capture {

void FrameQueue::push(const Frame &frame) {
  {
    const std::scoped_lock lock(mutex_);
    if (frames_.size() >= capacity_) {
      frames_.pop_front();
      ++dropped_;
    }
    frames_.push_back(frame);
  }
  ready_.notify_one();
}

std::optional<Frame> FrameQueue::pop(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  if (!ready_.wait_for(lock, timeout, [this] { return !frames_.empty(); }))
    return std::nullopt;

  Frame frame = std::move(frames_.front());
  frames_.pop_front();
  return frame;
}

std::size_t FrameQueue::dropped() const {
  const std::scoped_lock lock(mutex_);
  return dropped_;
}

Device::Device(Backend &backend, std::string id, std::string name, CameraMode mode,
               Settings settings)
    : id_(std::move(id)), name_(std::move(name)), mode_(mode), settings_(settings),
      thread_([this, &backend] { run(backend); }) {}

Device::~Device() {
  stop_.store(true);
  if (thread_.joinable())
    thread_.join();
}

DeviceState Device::state() const {
  const std::scoped_lock lock(mutex_);
  return state_;
}

std::string Device::lostReason() const {
  const std::scoped_lock lock(mutex_);
  return lostReason_;
}

std::string Device::describe() const {
  const std::scoped_lock lock(mutex_);
  return description_;
}

std::shared_ptr<FrameQueue> Device::subscribe(std::size_t capacity) {
  auto queue = std::make_shared<FrameQueue>(capacity);
  const std::scoped_lock lock(mutex_);
  queues_.push_back(queue);
  return queue;
}

void Device::unsubscribe(const std::shared_ptr<FrameQueue> &queue) {
  const std::scoped_lock lock(mutex_);
  std::erase(queues_, queue);
}

void Device::lose(const std::string &reason) {
  const std::scoped_lock lock(mutex_);
  state_ = DeviceState::Lost;
  lostReason_ = reason;
}

void Device::deliver(const Frame &frame) {
  const std::scoped_lock lock(mutex_);
  state_ = DeviceState::Running;
  for (const auto &queue : queues_)
    queue->push(frame);
}

void Device::run(Backend &backend) {
  std::unique_ptr<Capture> capture;
  try {
    capture = backend.open(id_, mode_);
  } catch (const CaptureError &error) {
    lose(error.what());
    return;
  } catch (const std::exception &error) {
    lose(error.what());
    return;
  }

  using Clock = std::chrono::steady_clock;
  auto lastFrame = Clock::now();
  bool anyFrame = false;
  std::uint64_t frames = 0;

  // Описание захвата обновляется раз в столько кадров: его читают из других
  // потоков, а сам захват принадлежит этому.
  constexpr std::uint64_t kDescribeEvery = 30;

  while (!stop_.load()) {
    std::optional<Frame> frame;
    try {
      frame = capture->next(settings_.poll);
    } catch (const CaptureError &error) {
      lose(error.what());
      break;
    }

    const auto now = Clock::now();
    if (frame) {
      lastFrame = now;
      anyFrame = true;
      deliver(*frame);
      if (++frames % kDescribeEvery == 0) {
        const std::string description = capture->describe();
        const std::scoped_lock lock(mutex_);
        description_ = description;
      }
      continue;
    }

    const auto allowed = anyFrame ? settings_.silence : settings_.warmUp;
    if (now - lastFrame > allowed) {
      lose(anyFrame ? "no frames" : "no first frame");
      break;
    }
  }

  const std::string description = capture->describe();
  const std::scoped_lock lock(mutex_);
  description_ = description;
}

DeviceRegistry::DeviceRegistry(Backend &backend, Device::Settings settings)
    : backend_(backend), settings_(settings) {}

std::shared_ptr<Device> DeviceRegistry::acquire(const std::string &id, const std::string &name,
                                                const CameraMode &mode) {
  const std::scoped_lock lock(mutex_);

  if (auto existing = devices_[id].lock(); existing && existing->state() != DeviceState::Lost)
    return existing;

  auto device = std::make_shared<Device>(backend_, id, name, mode, settings_);
  devices_[id] = device;
  return device;
}

std::shared_ptr<Device> DeviceRegistry::find(const std::string &id) const {
  const std::scoped_lock lock(mutex_);
  const auto found = devices_.find(id);
  return found == devices_.end() ? nullptr : found->second.lock();
}

} // namespace cam::capture
