// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
#include "BackgroundMailbox.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>
using namespace ed::experimental;
using Clock = std::chrono::steady_clock;
int main() {
  Queue<unsigned, 4> bounded;
  for (unsigned i = 0; i < 4; ++i) assert(bounded.push(i));
  assert(!bounded.push(99));
  unsigned value;
  for (unsigned i = 0; i < 4; ++i) { assert(bounded.pop(value)); assert(value == i); }
  assert(!bounded.pop(value));
  assert(!expired(UINT32_MAX - 10, 5));
  assert(expired(5, 5)); assert(expired(6, 5));
  assert(expired(0, UINT32_MAX));

  assert(commandAllowed(10, 20, false, 0, 5, 5));
  assert(!commandAllowed(10, 20, false, 0, 5, 0)); // lease expired while queued
  assert(!commandAllowed(10, 20, false, 0, 5, 6)); // different game
  assert(!commandAllowed(20, 20, false, 0, 5, 5)); // command expired
  assert(commandAllowed(10, 20, true, 15, 0, 0));
  assert(!commandAllowed(15, 20, true, 15, 0, 0));
  assert(!commandAllowed(10, 20, true, 0, 0, 0));
  assert(commandAllowed(UINT32_MAX - 5, 10, true, 5, 0, 0));

  // Exercise the actual mailbox memory ordering with different host threads.
  Queue<unsigned, 4> stream;
  constexpr unsigned count = 200000;
  std::thread producer([&] {
    for (unsigned i = 0; i < count; ++i)
      while (!stream.push(i)) std::this_thread::yield();
  });
  for (unsigned i = 0; i < count; ++i) {
    while (!stream.pop(value)) std::this_thread::yield();
    assert(value == i);
  }
  producer.join();

  struct Command { uint32_t deadline; unsigned sequence; };
  Request<Command> request;
  std::thread requester([&] {
    for (unsigned i = 0; i < 10000; ++i) {
      request.post({10, i});
      while (!request.finished()) std::this_thread::yield();
      assert(request.release() == (i % 2 == 0));
    }
  });
  for (unsigned i = 0; i < 10000; ++i) {
    Command *command;
    while (!(command = request.take())) std::this_thread::yield();
    assert(command->sequence == i);
    assert(request.take() == nullptr); // never double execute
    request.complete(i % 2 == 0);
  }
  requester.join();

  // Scheduling experiment: a busy network worker cannot own/lock local I/O.
  // This proves separation on host threads, not Arduino scheduler timing.
  const auto start = Clock::now();
  auto now = [&] { return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()); };
  std::atomic<bool> blocked{false}, done{false};
  std::thread network([&] {
    blocked = true;
    const auto until = Clock::now() + std::chrono::milliseconds(300);
    while (Clock::now() < until) {} // model a modem wait with no yield
    request.post({100, 1});
    while (!request.finished()) std::this_thread::yield();
    assert(!request.release()); // expired command must not run after the stall
    done = true;
  });
  while (!blocked) std::this_thread::yield();
  unsigned samples = 0, executions = 0;
  bool output = true;
  uint32_t stopped = 0;
  while (!done) {
    ++samples;
    if (output && expired(now(), 50)) { output = false; stopped = now(); }
    if (Command *command = request.take()) {
      bool accepted = !expired(now(), command->deadline);
      if (accepted) ++executions;
      request.complete(accepted);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  network.join();
  assert(!output && stopped < 250 && samples > 20 && executions == 0);
  std::cout << "200000 queued events, 10000 callback handoffs; local samples during stall: "
            << samples << "; output stopped at " << stopped << " ms\n";
}
