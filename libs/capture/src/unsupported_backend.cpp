#include "cam/capture/unsupported_backend.hpp"

#include <utility>

namespace cam::capture {

UnsupportedBackend::UnsupportedBackend(std::string system) : system_(std::move(system)) {}

std::unique_ptr<Capture> UnsupportedBackend::open(const std::string & /*id*/,
                                                  const CameraMode & /*mode*/) {
  throw CaptureError("capture is not supported on " + system_);
}

std::string UnsupportedBackend::unsupported() const {
  return "Camera capture on " + system_ + " is not supported yet.";
}

} // namespace cam::capture
