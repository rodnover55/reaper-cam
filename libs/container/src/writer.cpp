#include "cam/container/writer.hpp"

#include "mov_writer.hpp"

#include <stdexcept>
#include <utility>

namespace cam::container {

std::unique_ptr<VideoWriter> createWriter(std::unique_ptr<Output> output,
                                          const VideoFormat &format) {
  if (format.width <= 0 || format.height <= 0 || format.rateNumerator <= 0 ||
      format.rateDenominator <= 0)
    throw std::invalid_argument("размер кадра и частота должны быть положительными");

  return std::make_unique<detail::MovWriter>(std::move(output), format);
}

} // namespace cam::container
