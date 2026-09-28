#pragma once

// Узел устройства V4L2: владение дескриптором и ioctl с повтором на EINTR.

#include <cerrno>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace cam::capture_v4l2::detail {

class Device {
public:
  Device() = default;
  explicit Device(const std::string &path, int flags = O_RDWR | O_NONBLOCK | O_CLOEXEC)
      : fd_(::open(path.c_str(), flags)) {}

  ~Device() {
    if (fd_ >= 0)
      ::close(fd_);
  }

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  Device &operator=(Device &&other) noexcept {
    if (this != &other) {
      if (fd_ >= 0)
        ::close(fd_);
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  bool isOpen() const { return fd_ >= 0; }
  int fd() const { return fd_; }

  /// ioctl, переживающий прерывание сигналом. Возвращает 0 или errno.
  template <class T> int control(unsigned long request, T *argument) const {
    for (;;) {
      if (::ioctl(fd_, request, argument) == 0)
        return 0;
      if (errno != EINTR)
        return errno;
    }
  }

private:
  int fd_ = -1;
};

} // namespace cam::capture_v4l2::detail
