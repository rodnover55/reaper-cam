#include <doctest/doctest.h>

#include "cam/timing/spsc_ring.hpp"

#include <cstdint>
#include <thread>

using cam::timing::SpscRing;

TEST_CASE("кольцевой буфер: порядок, заполнение и отброс лишнего") {
  SpscRing<int, 4> ring;
  CHECK_FALSE(ring.pop());

  for (int i = 0; i < 4; ++i)
    CHECK(ring.push(i));
  CHECK_FALSE(ring.push(4)); // места нет — запись отброшена, а не ждёт
  CHECK(ring.dropped() == 1);

  for (int i = 0; i < 4; ++i)
    CHECK(ring.pop() == i);
  CHECK_FALSE(ring.pop());

  // Индексы идут дальше ёмкости — буфер оборачивается.
  for (int round = 0; round < 10; ++round) {
    CHECK(ring.push(round));
    CHECK(ring.pop() == round);
  }
}

TEST_CASE("кольцевой буфер: два потока — ничего не теряется и не путается") {
  struct Mark {
    std::uint64_t index;
    double time;
  };

  SpscRing<Mark, 1024> ring;
  constexpr std::uint64_t kCount = 1'000'000;

  std::thread producer([&ring] {
    for (std::uint64_t i = 0; i < kCount;) {
      if (ring.push({.index = i, .time = static_cast<double>(i) * 0.5}))
        ++i;
      else
        std::this_thread::yield();
    }
  });

  std::uint64_t expected = 0;
  bool ordered = true;
  while (expected < kCount) {
    if (const auto mark = ring.pop()) {
      ordered = ordered && mark->index == expected &&
                mark->time == static_cast<double>(expected) * 0.5;
      ++expected;
    }
  }

  producer.join();
  CHECK(ordered);
  CHECK(expected == kCount);
}
