#include "run_command.hpp"

#include <array>
#include <cstdio>

namespace cam::tests {

std::optional<std::string> runCommand(const std::string &command) {
  // Тестам нужен внешний ffprobe, и оболочка здесь — намеренно.
  std::FILE *pipe = popen((command + " 2>/dev/null").c_str(), "r"); // NOLINT(cert-env33-c)
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
