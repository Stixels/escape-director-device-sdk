// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ed {

// Remembers one session's commands until their envelopes expire. A duplicate
// within this window reports confirmation_unknown and must not execute again.
// The protocol adapter validates UUID syntax before calling admit().
template <size_t Capacity = 64> class CommandWindow {
public:
  enum class Admission { Accepted, Expired, InvalidIdentity, Duplicate, Full };

  void clear() {
    count = 0;
  }

  // These are station-clock timestamps, unlike the monotonic times in Authority.
  Admission admit(const char *id, int64_t expiresAt, int64_t now) {
    if (expiresAt <= now || uint64_t(expiresAt) - uint64_t(now) > MAX_LIFETIME_MS) {
      return Admission::Expired;
    }
    if (!id || strlen(id) != ID_LENGTH) {
      return Admission::InvalidIdentity;
    }

    removeExpired(now);
    for (size_t i = 0; i < count; ++i) {
      if (strcmp(entries[i].id, id) == 0) {
        return Admission::Duplicate;
      }
    }
    if (count == Capacity) {
      return Admission::Full;
    }

    memcpy(entries[count].id, id, ID_LENGTH + 1);
    entries[count].expiresAt = expiresAt;
    ++count;
    return Admission::Accepted;
  }

private:
  static constexpr size_t ID_LENGTH = 36;
  static constexpr uint64_t MAX_LIFETIME_MS = 3000;
  struct Entry {
    char id[ID_LENGTH + 1];
    int64_t expiresAt;
  };
  Entry entries[Capacity] = {};
  size_t count = 0;

  void removeExpired(int64_t now) {
    for (size_t i = 0; i < count;) {
      if (entries[i].expiresAt <= now) {
        // Order does not matter. Move the last entry here and inspect it next.
        --count;
        entries[i] = entries[count];
      } else {
        ++i;
      }
    }
  }
};

} // namespace ed
