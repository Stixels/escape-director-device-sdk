// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstdio>

#include <NetworkRetry.h>
#include <StationAddress.h>

namespace {
uint32_t ip(int a, int b, int c, int d) {
  return uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(c) << 8 | uint32_t(d);
}
} // namespace

int main() {
  assert(ed::commonPrefixBits(ip(192, 168, 1, 5), ip(192, 168, 1, 9)) == 28);
  assert(ed::commonPrefixBits(ip(10, 0, 0, 1), ip(10, 0, 0, 1)) == 32);
  assert(ed::commonPrefixBits(ip(10, 0, 0, 1), ip(192, 168, 0, 1)) == 0);

  // Bench case: VPN and Tailscale addresses sort first by text; the controller
  // (192.168.68.55/22) reaches the station on a routed 192.168.1.x segment.
  const uint32_t board = ip(192, 168, 68, 55);
  const uint32_t station[] = {ip(10, 2, 0, 2), ip(100, 109, 58, 46), ip(192, 168, 1, 151)};
  auto score = [&](size_t i) { return ed::commonPrefixBits(board, station[i]); };
  assert(ed::stationAddressIndex(0, 3, score) == 2); // the LAN address first
  const size_t second = ed::stationAddressIndex(1, 3, score);
  const size_t third = ed::stationAddressIndex(2, 3, score);
  assert(second != 2 && third != 2 && second != third); // every candidate stays in the cycle
  assert(ed::stationAddressIndex(3, 3, score) == 2);

  // Same subnet wins over another private range; ties keep the station's order.
  const uint32_t local[] = {ip(172, 16, 0, 9), ip(192, 168, 68, 10), ip(192, 168, 69, 20)};
  auto localScore = [&](size_t i) { return ed::commonPrefixBits(board, local[i]); };
  assert(ed::stationAddressIndex(0, 3, localScore) == 1);
  auto tie = [](size_t) { return 5; };
  assert(ed::stationAddressIndex(0, 3, tie) == 0 && ed::stationAddressIndex(2, 3, tie) == 2);
  assert(ed::stationAddressIndex(0, 0, tie) == 0);
  // A blocking attempt: the retry wait starts when it ends, not when it began.
  ed::NetworkRetry retry;
  retry.observe(true, 0);
  retry.observe(false, 20000);
  assert(!retry.ready(20500) && retry.ready(21000));
  retry.attempted(21000); // waits 1 s before the next attempt
  retry.finished(31000);  // but the attempt blocked for 10 s
  assert(!retry.ready(31500) && retry.ready(32000));
  std::puts("stationAddress passed");
  return 0;
}
