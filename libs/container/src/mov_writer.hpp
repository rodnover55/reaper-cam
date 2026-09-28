#pragma once

#include "cam/container/writer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cam::container::detail {

class Bytes;

class MovWriter : public VideoWriter {
public:
  MovWriter(std::unique_ptr<Output> output, const VideoFormat &format);
  ~MovWriter() override;

  MovWriter(const MovWriter &) = delete;
  MovWriter &operator=(const MovWriter &) = delete;
  MovWriter(MovWriter &&) = delete;
  MovWriter &operator=(MovWriter &&) = delete;

  void append(std::span<const std::uint8_t> jpeg) override;
  void finish() override;
  std::uint64_t frameCount() const override { return frames_; }

private:
  /// Где в индексе лежат длительности: они обновляются после каждого
  /// фрагмента. Смещения — от начала moov.
  struct DurationFields {
    std::size_t mvhd = 0;
    std::size_t tkhd = 0;
    std::size_t mdhd = 0;
    std::size_t mehd = 0;
  };

  void close();
  void movie(Bytes &b);
  void writeFragment();
  void patchDurations(std::uint64_t frames);
  std::uint32_t movieDurationOf(std::uint64_t frames) const;

  std::unique_ptr<Output> output_;
  VideoFormat format_;
  int framesPerFragment_;
  bool finished_ = false;
  std::uint64_t frames_ = 0;

  std::size_t moovAt_ = 0;
  DurationFields fields_;
  std::vector<std::vector<std::uint8_t>> pending_;
  std::uint64_t fragmentFirstFrame_ = 0;
  std::uint32_t sequence_ = 0;
};

} // namespace cam::container::detail
