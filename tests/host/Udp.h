// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
#pragma once
#include "Arduino.h"
class UDP : public Stream {
public:
  size_t write(uint8_t) override { return 1; }
};
