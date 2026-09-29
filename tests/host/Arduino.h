// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
//
// Host stand-in for the parts of the Arduino core that EscapeDirectorRoom.cpp
// uses. Tests control time and pin levels through the fake namespace.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#define HIGH 1
#define LOW 0
#define F(text) text
enum PinMode : uint8_t { INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2 };
constexpr uint8_t LED_BUILTIN = 13;

namespace fake {
inline uint32_t now = 0;
inline uint8_t levels[64] = {};
inline uint8_t modes[64] = {};
inline int masked = 0;
} // namespace fake

inline uint32_t millis() { return fake::now; }
inline void pinMode(uint8_t pin, PinMode mode) { fake::modes[pin] = mode; }
inline int digitalRead(uint8_t pin) { return fake::levels[pin]; }
inline void digitalWrite(uint8_t pin, int level) { fake::levels[pin] = uint8_t(level); }
inline void noInterrupts() { ++fake::masked; }
inline void interrupts() { --fake::masked; }

class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  size_t print(const char *text) {
    size_t n = 0;
    while (*text)
      n += write(uint8_t(*text++));
    return n;
  }
  size_t println(const char *text = "") { return print(text) + write('\n'); }
};

class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
};

class IPAddress {
public:
  IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) : bytes{a, b, c, d} {}
  uint8_t operator[](int i) const { return bytes[i]; }

private:
  uint8_t bytes[4];
};

class FakeSerial : public Stream {
public:
  std::string output;
  size_t write(uint8_t c) override {
    output += char(c);
    return 1;
  }
};
inline FakeSerial Serial;
