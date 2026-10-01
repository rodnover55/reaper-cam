#include "run_command.hpp"

#include <array>
#include <cstdio>

// У MSVC те же функции — под своими именами, а stderr глушится в NUL.
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
constexpr const char *kNoStderr = " 2>NUL";
#else
constexpr const char *kNoStderr = " 2>/dev/null";
#endif

namespace cam::tests {

std::optional<std::string> runCommand(const std::string &command) {
  // Тестам нужен внешний ffprobe, и оболочка здесь — намеренно.
  std::FILE *pipe = popen((command + kNoStderr).c_str(), "r"); // NOLINT(cert-env33-c)
  if (!pipe)
    return std::nullopt;

  std::string output;
  std::array<char, 4096> chunk{};
  while (const std::size_t got = std::fread(chunk.data(), 1, chunk.size(), pipe))
    output.append(chunk.data(), got);

  if (pclose(pipe) != 0)
    return std::nullopt;

  return output;
}

bool hasFfprobe() {
  static const bool found = runCommand("ffprobe -version").has_value();
  return found;
}

} // namespace cam::tests
