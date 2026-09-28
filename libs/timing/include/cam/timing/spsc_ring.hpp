#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace cam::timing {

/// Кольцевой буфер без блокировок: один пишет, один читает (design.md D5, D8).
///
/// Пишущий — звуковой поток или WriteDoubles: запись не ждёт, не выделяет
/// память и не зовёт систему. Если читающий отстал и места нет, запись
/// отбрасывается и считается: теряется отметка, а не звук.
///
/// Писать могут и разные потоки по очереди, если сами они упорядочены —
/// хост зовёт WriteDoubles одного экземпляра из разных рабочих потоков, но
/// никогда одновременно.
template <class T, std::size_t Capacity> class SpscRing {
  static_assert(std::has_single_bit(Capacity), "ёмкость — степень двойки");
  static_assert(std::is_trivially_copyable_v<T>,
                "в звуковом потоке — только простое копирование");

public:
  /// Пишущая сторона. Ложь — места нет, значение отброшено.
  bool push(const T &value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    slots_[head & (Capacity - 1)] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  /// Читающая сторона.
  std::optional<T> pop() noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire))
      return std::nullopt;

    const T value = slots_[tail & (Capacity - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return value;
  }

  /// Сколько записей отброшено из-за переполнения.
  std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
  // Счётчики пишущего и читающего — на разных строках кэша, между ними
  // записи: так стороны не мешают друг другу.
  alignas(64) std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> dropped_{0};
  std::array<T, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace cam::timing
