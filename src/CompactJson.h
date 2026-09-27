// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stddef.h>

namespace ed {

// Streams a JSON document without the whitespace between tokens, so a
// description can stay in flash instead of being copied into RAM to send.
// The source must already be valid JSON; strings and escapes pass through.
template <typename Emit> size_t forEachCompactJson(const char *json, Emit emit) {
  size_t count = 0;
  bool inString = false, escaped = false;
  for (; *json; ++json) {
    const char c = *json;
    if (inString) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        inString = false;
    } else if (c == '"') {
      inString = true;
    } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      continue;
    }
    emit(c);
    ++count;
  }
  return count;
}

inline size_t compactJsonLength(const char *json) {
  return forEachCompactJson(json, [](char) {});
}

} // namespace ed
