// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#include <initializer_list>

#include <cassert>
#include <climits>
#include <EscapeDirectorDevice.h>
#include "../examples/two_props/TwoProps.h"
int main() {
  using Window = ed::CommandWindow<2>;
  using A = Window::Admission;
  const char *a = example::TAPS_ID, *b = example::HOLD_ID;
  Window window;
  assert(window.admit(a, 1000, 0) == A::Accepted);
  assert(window.admit(a, 2000, 1) == A::Duplicate);
  assert(window.admit(b, 2000, 1) == A::Accepted);
  assert(window.admit("12345678-1234-4234-8234-123456789012", 2000, 1) == A::Full);
  assert(window.admit(a, 2000, 1000) == A::Accepted);
  assert(window.admit(a, 1000, 1000) == A::Expired);
  assert(window.admit(a, 5000, 1000) == A::Expired);
  assert(window.admit("short", 2000, 1000) == A::InvalidIdentity);
  window.clear();
  assert(window.admit(a, 2000, 1000) == A::Accepted);

  example::TwoProps props;
  ed::Authority authority(example::TwoProps::cleanup, &props);
  using M = ed::Authority::Mode;
  assert(!authority.enter(M::Game, 1000, 0));
  authority.connected();
  assert(authority.enter(M::Test, 1000, 0));
  auto test = authority.generation();
  assert(!authority.enter(M::Game, 1000, 1));
  props.taps.solved = true;
  props.taps.testPulse(0);
  props.hold.testPulse(0);
  assert(authority.renew(test, 1000, 999));
  authority.disconnect();
  assert(!props.hold.output() && props.taps.output()); // Normal solved output survives.
  assert(!props.taps.temporaryOutput);
  authority.connected();
  assert(!authority.renew(test, 1000, 1000));
  assert(!authority.allows(M::Game, authority.generation(), 1000));
  assert(authority.enter(M::Test, 1000, UINT32_MAX - 499));
  props.hold.testPulse(UINT32_MAX - 499);
  test = authority.generation();
  assert(authority.allows(M::Test, test, 499));
  assert(!authority.renew(test, 1000, 500));
  assert(!props.hold.temporaryOutput);
  assert(authority.enter(M::Game, 1000, 500));
  auto game = authority.generation();
  assert(authority.allows(M::Game, game, 501));
  assert(authority.leave(game, 502));
  assert(!authority.allows(M::Game, game, 502));

  props = {};
  assert(props.taps.complete());
  assert(props.taps.solved && props.taps.output());
  assert(!props.taps.complete());
  assert(!props.taps.sample(false, 5000));
  props.stopTest();
  assert(props.taps.output()); // Completion stays latched until reset.
  assert(!props.hold.output());
  props.taps.reset();
  assert(!props.taps.solved && !props.taps.output());
  assert(props.taps.complete()); // Reset permits a fresh completion signal.

  props = {};
  for (uint32_t t : {0u, 100u, 200u}) {
    assert(!props.taps.sample(true, t));
    props.taps.sample(true, t + 25);
    props.taps.sample(false, t + 50);
    props.taps.sample(false, t + 75);
  }
  assert(props.taps.solved && !props.hold.solved);
  // D3 emits once per debounced press, even if completion remains recorded.
  assert(!props.hold.sample(true, 0));
  assert(!props.hold.output());
  assert(!props.hold.sample(false, 10)); // Contact bounce must not activate it.
  assert(!props.hold.sample(true, 20));
  assert(!props.hold.sample(true, 44));
  assert(props.hold.sample(true, 45));
  assert(props.hold.output() && props.hold.solved);
  assert(!props.hold.sample(true, 100));
  assert(!props.hold.sample(false, 110));
  assert(props.hold.output());
  assert(!props.hold.sample(false, 135));
  assert(!props.hold.output() && props.hold.solved);
  assert(props.taps.output()); // Releasing D3 cannot affect the latched D2 prop.
  assert(!props.hold.sample(true, 200));
  assert(props.hold.sample(true, 225)); // Release rearms completion without Reset.
  assert(!props.hold.sample(true, 230)); // Holding cannot emit another completion.
  assert(props.hold.output());
  props.hold.sample(false, 250);
  assert(!props.hold.sample(true, 260)); // A short release bounce does not rearm.
  assert(!props.hold.sample(true, 285));
  props.hold.sample(false, 290);
  props.hold.sample(false, 315);

  assert(!props.hold.complete()); // An override still acts on an already-solved prop.
  props.hold.sample(false, 350);
  assert(props.hold.output());
  props.hold.reset();
  assert(!props.hold.solved && !props.hold.output());
  props.hold.sample(true, 400);
  assert(props.hold.sample(true, 425)); // Reset permits a fresh completion.
  props.hold.sample(false, 450);
  props.hold.sample(false, 475);
  props.hold.reset();
  props.hold.testPulse(500);
  props.hold.sample(false, 1499);
  assert(props.hold.output());
  props.hold.sample(false, 1500);
  assert(!props.hold.output()); // Explicit Test pulse still expires independently.
  // The momentary prop can complete a reopened Puzzle on any later press.
  // It does not need a remote reset, and cannot repeatedly fire while held.
  for (uint32_t t = 2000; t < 5000; t += 100) {
    assert(!props.hold.sample(true, t));
    assert(props.hold.sample(true, t + 25));
    assert(!props.hold.sample(true, t + 40));
    assert(props.hold.output());
    assert(!props.hold.sample(false, t + 50));
    assert(!props.hold.sample(false, t + 75));
    assert(!props.hold.output() && props.hold.solved);
    assert(props.taps.output());
  }
  props.taps.reset();
  assert(!props.taps.output());
}
