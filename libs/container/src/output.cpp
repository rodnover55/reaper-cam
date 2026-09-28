#include "cam/container/output.hpp"

#include <algorithm>
#include <fstream>
#include <ios>

namespace cam::container {

void MemoryOutput::append(std::span<const std::uint8_t> bytes) {
  data_.insert(data_.end(), bytes.begin(), bytes.end());
}

void MemoryOutput::patch(std::uint64_t offset, std::span<const std::uint8_t> bytes) {
  if (offset + bytes.size() > data_.size())
    throw OutputError("правка за концом записанного");

  std::ranges::copy(bytes, data_.begin() + static_cast<std::ptrdiff_t>(offset));
}

namespace {

class FileOutput : public Output {
public:
  explicit FileOutput(const std::filesystem::path &path)
      : stream_(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc) {
    if (!stream_)
      throw OutputError("не открылся файл " + path.string());
  }

  void append(std::span<const std::uint8_t> bytes) override {
    stream_.seekp(0, std::ios::end);
    write(bytes);
    size_ += bytes.size();
  }

  void patch(std::uint64_t offset, std::span<const std::uint8_t> bytes) override {
    if (offset + bytes.size() > size_)
      throw OutputError("правка за концом записанного");

    stream_.seekp(static_cast<std::streamoff>(offset));
    write(bytes);
  }

  std::uint64_t size() const override { return size_; }

  void flush() override {
    stream_.flush();
    if (!stream_)
      throw OutputError("запись в файл не удалась");
  }

private:
  void write(std::span<const std::uint8_t> bytes) {
    stream_.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    if (!stream_)
      throw OutputError("запись в файл не удалась");
  }

  std::fstream stream_;
  std::uint64_t size_ = 0;
};

} // namespace

std::unique_ptr<Output> openFileOutput(const std::filesystem::path &path) {
  return std::make_unique<FileOutput>(path);
}

} // namespace cam::container
