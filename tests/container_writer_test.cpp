#include <doctest/doctest.h>

#include "cam/capture/test_pattern.hpp"
#include "cam/container/writer.hpp"
#include "run_command.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using cam::capture::TestPattern;
using cam::container::createWriter;
using cam::container::MemoryOutput;
using cam::container::openFileOutput;
using cam::container::VideoFormat;

namespace {

std::filesystem::path scratchDirectory() {
  const auto dir = std::filesystem::temp_directory_path() / "reaper-cam-tests";
  std::filesystem::create_directories(dir);
  return dir;
}

void save(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

/// Ключ=значение из вывода ffprobe.
std::map<std::string, std::string> probe(const std::filesystem::path &file,
                                         const std::string &entries) {
  const auto output = cam::tests::runCommand(
      "ffprobe -v error -count_frames -select_streams v:0 "
      "-show_entries " +
      entries + " -of default=noprint_wrappers=1 '" + file.string() + "'");

  std::map<std::string, std::string> fields;
  if (!output)
    return fields;

  std::istringstream lines(*output);
  for (std::string line; std::getline(lines, line);) {
    const auto eq = line.find('=');
    if (eq != std::string::npos)
      fields[line.substr(0, eq)] = line.substr(eq + 1);
  }

  return fields;
}

/// Время показа каждого кадра по ffprobe, в секундах.
std::vector<double> probeFrameTimes(const std::filesystem::path &file) {
  const auto output = cam::tests::runCommand("ffprobe -v error -select_streams v:0 "
                                             "-show_entries frame=pts_time -of csv=p=0 '" +
                                             file.string() + "'");

  std::vector<double> times;
  if (!output)
    return times;

  std::istringstream lines(*output);
  for (std::string line; std::getline(lines, line);)
    if (!line.empty())
      times.push_back(std::stod(line));

  return times;
}

/// Коробки верхнего уровня: код и длина. Оборванная последняя коробка
/// попадает в список с длиной, которой в данных уже нет.
std::vector<std::pair<std::string, std::uint64_t>>
topLevelBoxes(const std::vector<std::uint8_t> &bytes) {
  std::vector<std::pair<std::string, std::uint64_t>> boxes;
  std::size_t at = 0;
  while (at + 8 <= bytes.size()) {
    const std::uint64_t size = (std::uint64_t{bytes[at]} << 24U) |
                               (std::uint64_t{bytes[at + 1]} << 16U) |
                               (std::uint64_t{bytes[at + 2]} << 8U) | bytes[at + 3];
    boxes.emplace_back(std::string(bytes.begin() + static_cast<std::ptrdiff_t>(at + 4),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(at + 8)),
                       size);
    if (size < 8)
      break;
    at += size;
  }
  return boxes;
}

/// Длительность из индекса (mvhd) в секундах: по ней хост берёт длину файла
/// (findings.md, вопрос 6).
double indexDuration(const std::vector<std::uint8_t> &bytes) {
  const std::string_view mvhd = "mvhd";
  const auto found = std::ranges::search(bytes, mvhd);
  REQUIRE_FALSE(found.empty());

  const auto at = static_cast<std::size_t>(found.begin() - bytes.begin()) + 4;
  const auto u32 = [&](std::size_t offset) {
    return (std::uint32_t{bytes[offset]} << 24U) | (std::uint32_t{bytes[offset + 1]} << 16U) |
           (std::uint32_t{bytes[offset + 2]} << 8U) | bytes[offset + 3];
  };

  // Версия и флаги, создан, изменён, шкала времени, длительность.
  return static_cast<double>(u32(at + 16)) / u32(at + 12);
}

} // namespace

TEST_CASE("писатель: три секунды кадров — 90 кадров MJPEG с частотой 30 и размером кадра") {
  const TestPattern pattern(320, 180);
  const auto path = scratchDirectory() / "writer.mov";

  auto writer = createWriter(
      openFileOutput(path),
      VideoFormat{.width = 320, .height = 180, .rateNumerator = 30, .rateDenominator = 1});
  for (int k = 0; k < 90; ++k)
    writer->append(pattern.render(k / 30.0, k / 30.0));
  writer->finish();

  CHECK(writer->frameCount() == 90);

  if (!cam::tests::hasFfprobe()) {
    MESSAGE("ffprobe не найден — проверка файла пропущена");
    return;
  }

  const auto stream =
      probe(path, "stream=codec_name,width,height,r_frame_rate,nb_read_frames");
  CHECK(stream.at("codec_name") == "mjpeg");
  CHECK(stream.at("width") == "320");
  CHECK(stream.at("height") == "180");
  CHECK(stream.at("r_frame_rate") == "30/1");
  CHECK(stream.at("nb_read_frames") == "90");

  // ffprobe печатает время с точностью до микросекунды.
  const auto times = probeFrameTimes(path);
  REQUIRE(times.size() == 90);
  for (std::size_t k = 0; k < times.size(); ++k) {
    CAPTURE(k);
    CHECK(std::abs(times[k] - (static_cast<double>(k) / 30.0)) < 1e-5);
  }
}

TEST_CASE("писатель: частота дробью — кадры 30000/1001 стоят на своих местах") {
  if (!cam::tests::hasFfprobe()) {
    MESSAGE("ffprobe не найден — проверка пропущена");
    return;
  }

  const TestPattern pattern(160, 90);
  const auto path = scratchDirectory() / "writer-ntsc.mov";

  auto writer = createWriter(openFileOutput(path), VideoFormat{.width = 160,
                                                               .height = 90,
                                                               .rateNumerator = 30000,
                                                               .rateDenominator = 1001});
  for (int k = 0; k < 60; ++k)
    writer->append(pattern.render(0.0, 0.0));
  writer->finish();

  const auto stream = probe(path, "stream=r_frame_rate,nb_read_frames");
  CHECK(stream.at("r_frame_rate") == "30000/1001");
  CHECK(stream.at("nb_read_frames") == "60");

  const auto times = probeFrameTimes(path);
  REQUIRE(times.size() == 60);
  for (std::size_t k = 0; k < times.size(); ++k) {
    CAPTURE(k);
    CHECK(std::abs(times[k] - (static_cast<double>(k) * 1001.0 / 30000.0)) < 1e-5);
  }
}

TEST_CASE("писатель: кадры лежат в контейнере байт в байт такими, какими пришли") {
  const TestPattern pattern(160, 90);

  auto output = std::make_unique<MemoryOutput>();
  const MemoryOutput &written = *output;
  auto writer = createWriter(
      std::move(output),
      VideoFormat{.width = 160, .height = 90, .rateNumerator = 30, .rateDenominator = 1});

  std::vector<std::vector<std::uint8_t>> frames;
  for (int k = 0; k < 45; ++k) {
    frames.push_back(pattern.render(k / 30.0, 100.0 + (k / 30.0)));
    writer->append(frames.back());
  }
  writer->finish();

  for (const auto &frame : frames)
    CHECK_FALSE(std::ranges::search(written.data(), frame).empty());
}

TEST_CASE("писатель: закрывает файл сам, при уничтожении") {
  const TestPattern pattern(160, 90);
  const auto path = scratchDirectory() / "writer-destroyed.mov";

  {
    auto writer = createWriter(
        openFileOutput(path),
        VideoFormat{.width = 160, .height = 90, .rateNumerator = 30, .rateDenominator = 1});
    for (int k = 0; k < 45; ++k) // полтора фрагмента, finish не зовётся
      writer->append(pattern.render(k / 30.0, 0.0));
  }

  std::ifstream file(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());

  const auto boxes = topLevelBoxes(bytes);
  const auto fragments =
      std::ranges::count_if(boxes, [](const auto &box) { return box.first == "moof"; });
  CHECK(fragments == 2);

  if (!cam::tests::hasFfprobe()) {
    MESSAGE("ffprobe не найден — проверка файла пропущена");
    return;
  }

  CHECK(indexDuration(bytes) == doctest::Approx(1.5));

  const auto stream = probe(path, "stream=nb_read_frames");
  CHECK(stream.at("nb_read_frames") == "45");
}

TEST_CASE(
    "писатель: файл, оборванный посреди записи, читается до последнего целого фрагмента") {
  const TestPattern pattern(160, 90);

  auto output = std::make_unique<MemoryOutput>();
  const MemoryOutput &written = *output;
  auto writer = createWriter(
      std::move(output),
      VideoFormat{.width = 160, .height = 90, .rateNumerator = 30, .rateDenominator = 1});

  // Снимки «диска»: после двух целых фрагментов и после трёх. Авария посреди
  // записи третьего — это второй снимок, у которого на диске успела лишь
  // часть третьего фрагмента, а длительности в индексе ещё от двух.
  for (int k = 0; k < 60; ++k)
    writer->append(pattern.render(k / 30.0, 0.0));
  const std::vector<std::uint8_t> twoFragments = written.data();

  for (int k = 60; k < 90; ++k)
    writer->append(pattern.render(k / 30.0, 0.0));
  const std::vector<std::uint8_t> threeFragments = written.data();

  const auto boxes = topLevelBoxes(twoFragments);
  REQUIRE(boxes.size() == 6);
  CHECK(boxes[0].first == "ftyp");
  CHECK(boxes[1].first == "moov");
  CHECK(boxes[2].first == "moof");
  CHECK(boxes[3].first == "mdat");
  CHECK(boxes[4].first == "moof");
  CHECK(boxes[5].first == "mdat");

  std::vector<std::uint8_t> crashed = twoFragments;
  const std::size_t third = threeFragments.size() - twoFragments.size();
  crashed.insert(
      crashed.end(), threeFragments.begin() + static_cast<std::ptrdiff_t>(twoFragments.size()),
      threeFragments.begin() + static_cast<std::ptrdiff_t>(twoFragments.size() + (third / 2)));

  // Длина в индексе — по последний целый фрагмент.
  CHECK(indexDuration(crashed) == doctest::Approx(2.0));

  if (!cam::tests::hasFfprobe()) {
    MESSAGE("ffprobe не найден — проверка файла пропущена");
    return;
  }

  const auto path = scratchDirectory() / "writer-crashed.mov";
  save(path, crashed);

  // ffprobe дочитывает и часть оборванного фрагмента; целые — всегда.
  const auto stream = probe(path, "stream=nb_read_frames");
  REQUIRE(stream.count("nb_read_frames") == 1);
  CHECK(std::stoi(stream.at("nb_read_frames")) >= 60);
}
