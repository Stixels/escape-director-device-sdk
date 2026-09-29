// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdint.h>
#include <initializer_list>
#include "StationClock.h"
#include "CommandWindow.h"

int main() {
  constexpr int64_t epoch = 1800000000000LL;
  ed::StationClock clock;
  clock.begin(epoch, 0);
  auto sequence = clock.request(0);
  assert(sequence != 0);
  assert(clock.request(1) == 0);
  assert(!clock.receive(sequence + 1, epoch + 50, 100));
  assert(clock.receive(sequence, epoch + 50, 100));
  assert(clock.now(100) == epoch + 100);
  assert(!clock.receive(sequence, epoch + 50, 100)); // duplicate reply

  sequence = clock.request(10000);
  assert(!clock.receive(sequence, epoch + 10050, 10501)); // delayed reply
  assert(clock.now(10501) == epoch + 10501);
  sequence = clock.request(20000);
  assert(!clock.receive(sequence, 0, 20001));
  assert(clock.receive(sequence, epoch + 20250, 20500)); // maximum accepted round trip
  sequence = clock.request(30000);
  clock.begin(epoch + 31000, 31000); // a new authenticated session
  assert(!clock.receive(sequence, epoch + 30050, 31001));
  assert(clock.request(31000) != sequence);

  // A slow or fast board must accept fresh commands and timestamp signals
  // correctly throughout a full day. Without resync, 0.2% drift is 173 seconds.
  for (int drift : {-2, 2}) {
    ed::StationClock day;
    day.begin(epoch, 0);
    for (uint32_t elapsed = 0; elapsed < 86400000; elapsed += 11000) {
      const uint32_t local = uint64_t(elapsed) * (1000 + drift) / 1000;
      const uint32_t token = day.request(local);
      assert(token);
      assert(day.receive(token, epoch + elapsed + 50, local + 100));
      const int64_t estimated = day.now(local + 500);
      const int64_t actual = epoch + elapsed + 500;
      assert(estimated == actual);
      ed::CommandWindow<> commands;
      const char *id = "00000000-0000-4000-8000-000000000001";
      using Admission = ed::CommandWindow<>::Admission;
      assert(commands.admit(id, actual + 2500, estimated) == Admission::Accepted);
      assert(commands.admit(id, actual + 2500, estimated) == Admission::Duplicate);
      assert(commands.admit(id, actual - 1, estimated) == Admission::Expired);
    }
  }

  clock.begin(epoch, UINT32_MAX - 100);
  sequence = clock.request(UINT32_MAX - 100);
  assert(clock.receive(sequence, epoch + 100, 99));
  assert(clock.now(199) == epoch + 300);
  assert(clock.request(9999));
}
