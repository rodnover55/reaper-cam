#include <doctest/doctest.h>

#include "cam/capture_v4l2/v4l2.hpp"

#include <filesystem>
#include <fstream>

using cam::capture_v4l2::resolveCamera;

namespace fs = std::filesystem;

namespace {

/// Временное дерево, как в /dev: узлы video*, ссылки в v4l/by-id и v4l/by-path.
struct DevTree {
  fs::path root = fs::temp_directory_path() / "reaper-cam-tests" / "dev";

  DevTree() {
    fs::remove_all(root);
    fs::create_directories(root / "v4l" / "by-id");
    fs::create_directories(root / "v4l" / "by-path");
    for (const char *node : {"video0", "video1", "video2"})
      std::ofstream(root / node).put('\0');
  }

  ~DevTree() { fs::remove_all(root); }

  DevTree(const DevTree &) = delete;
  DevTree &operator=(const DevTree &) = delete;
  DevTree(DevTree &&) = delete;
  DevTree &operator=(DevTree &&) = delete;

  /// Ссылка, как её делает udev: относительная, на два каталога вверх.
  void link(const char *kind, const char *name, const char *node) const {
    const fs::path at = root / "v4l" / kind / name;
    fs::remove(at);
    fs::create_symlink(fs::path("..") / ".." / node, at);
  }

  fs::path links() const { return root / "v4l"; }
};

constexpr const char *kCamera =
    "usb-CN09357G8LG009BLAFRPA01_Integrated_Webcam_HD-video-index0";

} // namespace

TEST_CASE("признак камеры: после смены цели ссылки разрешается в новый узел") {
  const DevTree tree;
  tree.link("by-id", kCamera, "video0");

  const std::string id = std::string("by-id/") + kCamera;
  const auto before = resolveCamera(id, tree.links());
  REQUIRE(before);
  CHECK(*before == tree.root / "video0");

  // После перезагрузки система выдала камере другой номер.
  tree.link("by-id", kCamera, "video2");

  const auto after = resolveCamera(id, tree.links());
  REQUIRE(after);
  CHECK(*after == tree.root / "video2");
}

TEST_CASE("признак камеры: by-path, отключённая камера и чужие пути") {
  const DevTree tree;
  tree.link("by-path", "pci-0000:00:14.0-usb-0:5:1.0-video-index0", "video1");

  const auto byPath =
      resolveCamera("by-path/pci-0000:00:14.0-usb-0:5:1.0-video-index0", tree.links());
  REQUIRE(byPath);
  CHECK(*byPath == tree.root / "video1");

  CHECK_FALSE(resolveCamera(std::string("by-id/") + kCamera, tree.links())); // ссылки нет
  CHECK_FALSE(resolveCamera("video0", tree.links()));
  CHECK_FALSE(resolveCamera("by-id/../../video0", tree.links()));
  CHECK_FALSE(resolveCamera("/dev/video0", tree.links()));
}
