// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <string.h>
#include <string>
#include "CompactJson.h"

std::string compact(const char *json) {
  std::string out;
  const size_t count = ed::forEachCompactJson(json, [&](char c) { out += c; });
  assert(count == out.size());
  assert(ed::compactJsonLength(json) == out.size());
  return out;
}

int main() {
  // Whitespace between tokens is dropped, including a pretty-printed manifest.
  assert(compact("{\n  \"a\": [1, 2],\r\n\t\"b\": true\n}") == "{\"a\":[1,2],\"b\":true}");
  // Whitespace, quotes and backslashes inside strings pass through unchanged.
  assert(compact("{ \"name\": \"Three  taps\" }") == "{\"name\":\"Three  taps\"}");
  assert(compact("[\"a \\\" b\", \" c\\\\\", \"d\"]") == "[\"a \\\" b\",\" c\\\\\",\"d\"]");
  assert(compact("\"\\\\\" ") == "\"\\\\\"");
  assert(compact("") == "");
  return 0;
}
