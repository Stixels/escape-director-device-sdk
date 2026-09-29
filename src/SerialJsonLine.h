// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stddef.h>
#include <stdint.h>

// ArduinoJson can read directly from this bounded frame without a second JSON
// string in RAM. A malformed line cannot consume the following USB request.
template <typename Input> class SerialJsonLine {
public:
  explicit SerialJsonLine(Input &input) : input(input) {}

  int read() {
    if (ended || remaining == 0)
      return -1;
    char value;
    if (input.readBytes(&value, 1) != 1)
      return -1;
    --remaining;
    if (value == '\n') {
      ended = true;
      return -1;
    }
    return static_cast<uint8_t>(value);
  }

  size_t readBytes(char *buffer, size_t count) {
    size_t copied = 0;
    for (; copied < count; ++copied) {
      const int value = read();
      if (value < 0)
        break;
      buffer[copied] = static_cast<char>(value);
    }
    return copied;
  }

  bool complete() const { return ended; }

private:
  Input &input;
  size_t remaining = 32768;
  bool ended = false;
};
