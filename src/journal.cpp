#include "journal.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace cam::reaper {
namespace {

std::mutex journalMutex;
std::FILE *journalFile = nullptr;

using Clock = std::chrono::steady_clock;
Clock::time_point journalStart = Clock::now();

/// Короткое имя потока: номера раздаются по первому появлению. Поток, который
/// открыл журнал, — главный, и он называется `main`.
std::atomic<int> nextThreadNumber{0};

std::string threadTag() {
  thread_local const int number = nextThreadNumber.fetch_add(1);
  return number == 0 ? std::string("main") : std::format("T{}", number);
}

} // namespace

void openJournal(const std::string &path) {
  const std::scoped_lock lock(journalMutex);

  if (journalFile)
    return;

  journalFile = std::fopen(path.c_str(), "a");
  journalStart = Clock::now();

  // Главный поток получает номер первым — до того, как REAPER позовёт
  // расширение из других.
  (void)threadTag();
}

void closeJournal() {
  const std::scoped_lock lock(journalMutex);

  if (journalFile)
    (void)std::fclose(journalFile);

  journalFile = nullptr;
}

double journalSeconds(std::chrono::steady_clock::time_point at) {
  return std::chrono::duration<double>(at - journalStart).count();
}

void journalLine(std::string_view line) {
  const std::string tag = threadTag();
  const double seconds = std::chrono::duration<double>(Clock::now() - journalStart).count();
  const std::string text = std::format("{:12.6f} {:>4} {}\n", seconds, tag, line);

  const std::scoped_lock lock(journalMutex);

  if (!journalFile)
    return;

  (void)std::fputs(text.c_str(), journalFile);
  (void)std::fflush(journalFile);
}

} // namespace cam::reaper
