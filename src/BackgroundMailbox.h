// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace ed { namespace experimental {
// One producer, one consumer. Payload publication/reuse are synchronized;
// neither side waits for the other. False means full/empty, never overwrite.
template <typename T, size_t Capacity> class Queue {
  static_assert(Capacity > 0, "nonempty queue");
  T slots[Capacity + 1]{};
  std::atomic<size_t> read{0}, write{0};
public:
  bool push(const T &value) {
    const size_t w = write.load(std::memory_order_relaxed), next = (w + 1) % (Capacity + 1);
    if (next == read.load(std::memory_order_acquire)) return false;
    slots[w] = value;
    write.store(next, std::memory_order_release);
    return true;
  }
  bool pop(T &value) {
    const size_t r = read.load(std::memory_order_relaxed);
    if (r == write.load(std::memory_order_acquire)) return false;
    value = slots[r];
    read.store((r + 1) % (Capacity + 1), std::memory_order_release);
    return true;
  }
};
inline bool expired(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
inline bool commandAllowed(uint32_t now, uint32_t deadline, bool testing,
                           uint32_t testUntil, uint32_t capturedGame, uint32_t currentGame) {
  if (expired(now, deadline)) return false;
  return testing ? testUntil && !expired(now, testUntil)
                 : capturedGame && capturedGame == currentGame;
}
// Network worker publishes one request and waits for its acknowledgement.
// Request pointers stay alive until Done. No mutex is held across driver code.
template <typename T> class Request {
  enum State { Idle, Pending, Running, Done };
  std::atomic<State> state{Idle};
  T value{};
  bool result = false;
public:
  void post(const T &request) { value = request; state.store(Pending, std::memory_order_release); }
  T *take() {
    State expected = Pending;
    return state.compare_exchange_strong(expected, Running, std::memory_order_acquire) ? &value : nullptr;
  }
  void complete(bool accepted) { result = accepted; state.store(Done, std::memory_order_release); }
  bool finished() const { return state.load(std::memory_order_acquire) == Done; }
  bool release() { bool accepted = result; state.store(Idle); return accepted; }
};
} }
