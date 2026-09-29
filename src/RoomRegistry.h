// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
// Arduino-independent core of ed::Room: declarations, contract validation,
// stable prop IDs, the generated description and delayed actions. Strings are
// the sketch's literals; nothing here copies them into RAM.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ed {
namespace room {

using Action = void (*)();

enum class Kind : uint8_t { Signal, Command, Boolean, Number, Enum };

// Commands with a Test behavior and number/enum state carry details.
struct Detail {
  Action testStart = nullptr, testStop = nullptr;
  int16_t testPin = -1;
  uint8_t testLevel = 1;
  uint32_t testMs = 0;
  int32_t min = 0, max = 0;
  const char *unit = nullptr;
  const char *const *values = nullptr;
  uint8_t valueCount = 0;
};

struct Entry {
  const char *id = nullptr, *name = nullptr;
  union {
    Action action;
    const bool *boolean;
    const int32_t *number;
    const uint8_t *index;
  } ref{nullptr};
  uint8_t prop = 0;
  Kind kind = Kind::Signal;
  uint8_t flags = 0; // Testable, LastCondition
  int8_t detail = -1;
};
constexpr uint8_t Testable = 1, LastCondition = 2, TestActive = 4;

struct Prop {
  const char *slug = nullptr, *name = nullptr, *uuid = nullptr;
  const char *completionSignal = nullptr, *completionCommand = nullptr;
};

// Contract limits (description.schema.json).
constexpr size_t MAX_PROPS = 8, MAX_SIGNALS = 8, MAX_COMMANDS = 8, MAX_STATE = 16,
                 MAX_ENUM_VALUES = 16, MAX_NAME = 80, MAX_UNIT = 16, MAX_FIRMWARE = 40;

inline bool validId(const char *id) {
  if (!id || *id < 'a' || *id > 'z')
    return false;
  size_t length = 0;
  for (const char *c = id; *c; ++c, ++length)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-'))
      return false;
  return length <= 40;
}
inline bool validText(const char *text, size_t max) {
  return text && *text && strlen(text) <= max;
}
inline bool validUuid(const char *uuid) {
  if (!uuid || strlen(uuid) != 36)
    return false;
  for (size_t i = 0; i < 36; ++i) {
    const char c = uuid[i];
    const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? c != '-'
             : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
      return false;
  }
  return uuid[14] >= '1' && uuid[14] <= '8' &&
         (uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b' ||
          uuid[19] == 'A' || uuid[19] == 'B');
}

// A stable UUID (version 8, vendor-defined) derived from a prop slug. Prop IDs
// need only be unique within one Device, so the slug alone identifies it.
inline void derivedUuid(const char *slug, char out[37]) {
  uint64_t a = 0xcbf29ce484222325ULL, b = 0x84222325cbf29ce4ULL;
  for (const char *p = "escape-director-room:"; *p; ++p) {
    a = (a ^ uint8_t(*p)) * 0x100000001b3ULL;
    b = (b ^ uint8_t(*p)) * 0x100000001b3ULL;
  }
  for (const char *p = slug; *p; ++p) {
    a = (a ^ uint8_t(*p)) * 0x100000001b3ULL;
    b = (b ^ (uint8_t(*p) + 0x5b)) * 0x100000001b3ULL;
  }
  uint8_t bytes[16];
  for (int i = 0; i < 8; ++i) {
    bytes[i] = uint8_t(a >> (56 - 8 * i));
    bytes[8 + i] = uint8_t(b >> (56 - 8 * i));
  }
  bytes[6] = uint8_t((bytes[6] & 0x0f) | 0x80); // version 8
  bytes[8] = uint8_t((bytes[8] & 0x3f) | 0x80); // RFC 4122 variant
  static const char hex[] = "0123456789abcdef";
  size_t at = 0;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      out[at++] = '-';
    out[at++] = hex[bytes[i] >> 4];
    out[at++] = hex[bytes[i] & 15];
  }
  out[at] = 0;
}

// Writes JSON through a sink with write(char). Only structure and escaping.
template <typename Sink> class JsonWriter {
public:
  explicit JsonWriter(Sink &sink) : sink(sink) {}
  void raw(const char *text) {
    while (*text)
      sink.write(*text++);
  }
  void string(const char *text) {
    sink.write('"');
    static const char hex[] = "0123456789abcdef";
    for (const char *c = text; *c; ++c) {
      const uint8_t value = uint8_t(*c);
      if (value == '"' || value == '\\') {
        sink.write('\\');
        sink.write(char(value));
      } else if (value < 0x20) {
        raw("\\u00");
        sink.write(hex[value >> 4]);
        sink.write(hex[value & 15]);
      } else {
        sink.write(char(value));
      }
    }
    sink.write('"');
  }
  void number(int32_t value) {
    char text[12];
    size_t n = 0;
    uint32_t magnitude = value < 0 ? uint32_t(0) - uint32_t(value) : uint32_t(value);
    do {
      text[n++] = char('0' + magnitude % 10);
      magnitude /= 10;
    } while (magnitude);
    if (value < 0)
      sink.write('-');
    while (n)
      sink.write(text[--n]);
  }
  void key(const char *name) {
    string(name);
    sink.write(':');
  }

private:
  Sink &sink;
};

struct CountingSink {
  size_t bytes = 0;
  void write(char) { ++bytes; }
};

class Registry {
public:
  Registry(Prop *props, size_t propCapacity, Entry *entries, size_t entryCapacity,
           Detail *details, size_t detailCapacity)
      : props(props), propCapacity(propCapacity), entries(entries),
        entryCapacity(entryCapacity), details(details), detailCapacity(detailCapacity) {}

  // Declarations record the first problem; begin() reports it.
  int addProp(const char *slug, const char *name, const char *uuid) {
    if (propCount >= propCapacity || propCount >= MAX_PROPS)
      return fail("too many props"), -1;
    if (!validId(slug))
      return fail("prop slug must match ^[a-z][a-z0-9-]{0,39}$"), -1;
    if (!validText(name, MAX_NAME))
      return fail("prop name must be 1-80 characters"), -1;
    if (uuid && !validUuid(uuid))
      return fail("explicit prop UUID is not a valid UUID"), -1;
    for (size_t i = 0; i < propCount; ++i)
      if (!strcmp(props[i].slug, slug))
        return fail("prop slugs must be unique"), -1;
    props[propCount] = Prop{slug, name, uuid};
    return int(propCount++);
  }

  int add(uint8_t prop, Kind kind, const char *id, const char *name) {
    if (prop >= propCount)
      return fail("capability declared on an unknown prop"), -1;
    if (entryCount >= entryCapacity)
      return fail("too many signals, commands and state fields for this Room's capacity"), -1;
    if (!validId(id))
      return fail("IDs must match ^[a-z][a-z0-9-]{0,39}$"), -1;
    if (!validText(name, MAX_NAME))
      return fail("names must be 1-80 characters"), -1;
    const bool state = kind == Kind::Boolean || kind == Kind::Number || kind == Kind::Enum;
    size_t same = 0;
    for (size_t i = 0; i < entryCount; ++i) {
      const Entry &e = entries[i];
      if (e.prop != prop || group(e.kind) != group(kind))
        continue;
      ++same;
      if (!strcmp(e.id, id))
        return fail("IDs must be unique within a prop's signals, commands or state"), -1;
    }
    const size_t limit = kind == Kind::Signal ? MAX_SIGNALS : kind == Kind::Command ? MAX_COMMANDS : MAX_STATE;
    if (same >= limit)
      return fail(state ? "a prop has more than 16 state fields" : "a prop has more than 8 signals or commands"), -1;
    Entry &e = entries[entryCount];
    e = Entry{};
    e.id = id;
    e.name = name;
    e.prop = prop;
    e.kind = kind;
    return int(entryCount++);
  }

  Detail *detailFor(int entry) {
    if (entry < 0)
      return nullptr;
    Entry &e = entries[entry];
    if (e.detail >= 0)
      return &details[e.detail];
    if (detailCount >= detailCapacity) {
      fail("too many Test behaviors and number/enum fields for this Room's capacity");
      return nullptr;
    }
    e.detail = int8_t(detailCount);
    details[detailCount] = Detail{};
    return &details[detailCount++];
  }

  // Contract checks that need the complete declaration set.
  const char *validate(const char *name, const char *firmwareVersion) {
    if (problem)
      return problem;
    if (!validText(name, MAX_NAME))
      return "the Room name must be 1-80 characters";
    if (!validText(firmwareVersion, MAX_FIRMWARE))
      return "the firmware version must be 1-40 characters";
    if (!propCount)
      return "declare at least one prop";
    for (size_t i = 0; i < entryCount; ++i) {
      const Entry &e = entries[i];
      const Detail *d = e.detail >= 0 ? &details[e.detail] : nullptr;
      if (e.kind == Kind::Number && d && (d->min > d->max || (d->unit && !validText(d->unit, MAX_UNIT))))
        return "number state needs min <= max and a unit of 1-16 characters";
      if (e.kind == Kind::Enum) {
        if (!d || !d->values || d->valueCount == 0 || d->valueCount > MAX_ENUM_VALUES)
          return "enum state needs 1-16 values";
        for (uint8_t v = 0; v < d->valueCount; ++v)
          if (!validId(d->values[v]))
            return "enum values must match ^[a-z][a-z0-9-]{0,39}$";
      }
    }
    for (size_t p = 0; p < propCount; ++p) {
      const Prop &prop = props[p];
      if (!prop.completionSignal)
        continue;
      if (find(uint8_t(p), Kind::Signal, prop.completionSignal) < 0 ||
          find(uint8_t(p), Kind::Command, prop.completionCommand) < 0)
        return "completion must name a declared signal and command of the same prop";
    }
    return nullptr;
  }

  void propId(uint8_t prop, char out[37]) const {
    if (props[prop].uuid) {
      for (int i = 0; i < 36; ++i) {
        const char c = props[prop].uuid[i];
        out[i] = c >= 'A' && c <= 'F' ? char(c - 'A' + 'a') : c;
      }
      out[36] = 0;
    } else {
      derivedUuid(props[prop].slug, out);
    }
  }
  int findProp(const char *uuid) const {
    char id[37];
    for (size_t p = 0; p < propCount; ++p) {
      propId(uint8_t(p), id);
      if (uuid && strlen(uuid) == 36 && equalsIgnoringCase(id, uuid))
        return int(p);
    }
    return -1;
  }
  int find(uint8_t prop, Kind kind, const char *id) const {
    for (size_t i = 0; i < entryCount; ++i)
      if (entries[i].prop == prop && group(entries[i].kind) == group(kind) && id &&
          !strcmp(entries[i].id, id))
        return int(i);
    return -1;
  }
  int findProp(const char *slug, bool bySlug) const {
    for (size_t p = 0; bySlug && p < propCount; ++p)
      if (!strcmp(props[p].slug, slug))
        return int(p);
    return -1;
  }

  template <typename Sink>
  void writeDescription(Sink &sink, const char *name, const char *firmwareVersion) const {
    JsonWriter<Sink> json(sink);
    json.raw("{");
    json.key("contractVersion");
    json.raw("1,");
    json.key("firmwareVersion");
    json.string(firmwareVersion);
    json.raw(",");
    json.key("name");
    json.string(name);
    json.raw(",");
    json.key("configuration");
    json.string("firmware-owned");
    json.raw(",");
    json.key("diagnostics");
    json.raw("{\"transitions\":false},");
    json.key("props");
    json.raw("[");
    char id[37];
    for (size_t p = 0; p < propCount; ++p) {
      if (p)
        json.raw(",");
      propId(uint8_t(p), id);
      json.raw("{");
      json.key("id");
      json.string(id);
      json.raw(",");
      json.key("name");
      json.string(props[p].name);
      writeGroup(json, uint8_t(p), Kind::Signal, "signals");
      writeGroup(json, uint8_t(p), Kind::Command, "commands");
      writeGroup(json, uint8_t(p), Kind::Boolean, "state");
      if (props[p].completionSignal) {
        json.raw(",");
        json.key("completion");
        json.raw("{");
        json.key("signal");
        json.string(props[p].completionSignal);
        json.raw(",");
        json.key("command");
        json.string(props[p].completionCommand);
        json.raw("}");
      }
      json.raw("}");
    }
    json.raw("]}");
  }
  size_t descriptionLength(const char *name, const char *firmwareVersion) const {
    CountingSink counter;
    writeDescription(counter, name, firmwareVersion);
    return counter.bytes;
  }

  Prop *props;
  size_t propCapacity, propCount = 0;
  Entry *entries;
  size_t entryCapacity, entryCount = 0;
  Detail *details;
  size_t detailCapacity, detailCount = 0;
  const char *problem = nullptr;

private:
  static int group(Kind kind) {
    return kind == Kind::Signal ? 0 : kind == Kind::Command ? 1 : 2;
  }
  static bool equalsIgnoringCase(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
      char x = *a, y = *b;
      if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
      if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
      if (x != y)
        return false;
    }
    return *a == *b;
  }
  void fail(const char *message) {
    if (!problem)
      problem = message;
  }
  template <typename Json>
  void writeGroup(Json &json, uint8_t prop, Kind kind, const char *label) const {
    json.raw(",");
    json.key(label);
    json.raw("[");
    bool first = true;
    for (size_t i = 0; i < entryCount; ++i) {
      const Entry &e = entries[i];
      if (e.prop != prop || group(e.kind) != group(kind))
        continue;
      if (!first)
        json.raw(",");
      first = false;
      json.raw("{");
      json.key("id");
      json.string(e.id);
      json.raw(",");
      json.key("name");
      json.string(e.name);
      const Detail *d = e.detail >= 0 ? &details[e.detail] : nullptr;
      if (e.kind == Kind::Command) {
        json.raw(",");
        json.key("testable");
        json.raw(e.flags & Testable ? "true" : "false");
      } else if (e.kind == Kind::Boolean) {
        json.raw(",");
        json.key("type");
        json.string("boolean");
      } else if (e.kind == Kind::Number) {
        json.raw(",");
        json.key("type");
        json.string("number");
        json.raw(",");
        json.key("min");
        json.number(d ? d->min : 0);
        json.raw(",");
        json.key("max");
        json.number(d ? d->max : 0);
        if (d && d->unit) {
          json.raw(",");
          json.key("unit");
          json.string(d->unit);
        }
      } else if (e.kind == Kind::Enum) {
        json.raw(",");
        json.key("type");
        json.string("enum");
        json.raw(",");
        json.key("values");
        json.raw("[");
        for (uint8_t v = 0; d && v < d->valueCount; ++v) {
          if (v)
            json.raw(",");
          json.string(d->values[v]);
        }
        json.raw("]");
      }
      json.raw("}");
    }
    json.raw("]");
  }
};

// Delayed actions keyed by function: scheduling one that is pending replaces
// it. Runs from the sketch loop, never from an interrupt.
template <size_t Capacity> class Scheduler {
public:
  bool after(Action action, uint32_t delayMs, uint32_t now, bool testScoped) {
    if (!action)
      return false;
    Item *free = nullptr;
    for (Item &item : items) {
      if (item.action == action) {
        item.due = now + delayMs;
        item.testScoped = testScoped;
        return true;
      }
      if (!item.action && !free)
        free = &item;
    }
    if (!free)
      return false;
    *free = Item{action, now + delayMs, testScoped};
    return true;
  }
  void cancel(Action action) {
    for (Item &item : items)
      if (item.action == action)
        item.action = nullptr;
  }
  void cancelAll() {
    for (Item &item : items)
      item.action = nullptr;
  }
  void cancelTestScoped() {
    for (Item &item : items)
      if (item.testScoped)
        item.action = nullptr;
  }
  bool pending(Action action) const {
    for (const Item &item : items)
      if (item.action == action)
        return true;
    return false;
  }
  // Runs every due action once; an action may schedule itself again.
  void runDue(uint32_t now) {
    for (Item &item : items) {
      if (!item.action || int32_t(now - item.due) < 0)
        continue;
      const Action action = item.action;
      item.action = nullptr;
      action();
    }
  }

private:
  struct Item {
    Action action = nullptr;
    uint32_t due = 0;
    bool testScoped = false;
  };
  Item items[Capacity];
};

} // namespace room
} // namespace ed
