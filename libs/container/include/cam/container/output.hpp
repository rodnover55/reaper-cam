#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace cam::container {

/// Куда писатель кладёт байты контейнера. Пишется только в конец; назад
/// писатель возвращается лишь затем, чтобы дописать длины и длительности в
/// уже записанные заголовки, когда они стали известны.
class Output {
public:
  virtual ~Output() = default;

  Output() = default;
  Output(const Output &) = delete;
  Output &operator=(const Output &) = delete;
  Output(Output &&) = delete;
  Output &operator=(Output &&) = delete;

  virtual void append(std::span<const std::uint8_t> bytes) = 0;

  /// Переписывает уже записанные байты начиная с `offset`. Длина файла не
  /// меняется.
  virtual void patch(std::uint64_t offset, std::span<const std::uint8_t> bytes) = 0;

  /// Сколько байтов записано.
  virtual std::uint64_t size() const = 0;

  /// Отдаёт записанное на диск. Писатель зовёт это после каждого целого куска,
  /// который читатель уже может разобрать.
  virtual void flush() = 0;
};

/// Вывод в память — для тестов.
class MemoryOutput : public Output {
public:
  void append(std::span<const std::uint8_t> bytes) override;
  void patch(std::uint64_t offset, std::span<const std::uint8_t> bytes) override;
  std::uint64_t size() const override { return data_.size(); }
  void flush() override {}

  const std::vector<std::uint8_t> &data() const { return data_; }

private:
  std::vector<std::uint8_t> data_;
};

/// Вывод в файл. Файл создаётся заново.
std::unique_ptr<Output> openFileOutput(const std::filesystem::path &path);

/// Ошибка вывода: файл не открылся или запись не удалась.
class OutputError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace cam::container
