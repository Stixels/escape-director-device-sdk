// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <cassert>
#include <CommandWindow.h>

// Command IDs are admitted once within their expiry window.
int main() {
  using Window = ed::CommandWindow<2>;
  using A = Window::Admission;
  const char *a = "0210a18f-c31f-45b7-a35a-e20eb82a6c11", *b = "7c1f3b2e-2d4a-4f61-9a8e-5b0c6d7e8f90";
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
}
