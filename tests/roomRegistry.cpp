// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstdio>
#include <string>

#include <RoomRegistry.h>

using namespace ed::room;

namespace {
int calls = 0, closes = 0;
void open() { ++calls; }
void close() { ++closes; }

struct StringSink {
  std::string text;
  void write(char c) { text += c; }
};
} // namespace

int main(int argc, char **argv) {
  // Stable, valid and distinct derived IDs.
  char a[37], b[37], again[37];
  derivedUuid("exit-door", a);
  derivedUuid("exit-door", again);
  derivedUuid("fuses", b);
  assert(std::string(a) == again);
  assert(std::string(a) != b);
  assert(validUuid(a) && validUuid(b));
  assert(a[14] == '8');

  assert(validId("joystick") && validId("rat-maglock") && validId("a"));
  assert(!validId("Joystick") && !validId("1st") && !validId("") && !validId(nullptr));
  assert(!validId("abcdefghijklmnopqrstuvwxyzabcdefghijklmno")); // 41 characters

  Prop props[8];
  Entry entries[24];
  Detail details[4];
  Registry r(props, 8, entries, 24, details, 4);
  bool opened = false, solved = true;
  int32_t progress = 3;
  uint8_t mode = 1;
  static const char *const modes[] = {"idle", "armed", "open"};

  const int door = r.addProp("exit-door", "Exit door", nullptr);
  const int stick = r.addProp("joystick", "Joystick \"sequence\"",
                              "0210A18F-C31F-45B7-A35A-E20EB82A6C11");
  assert(door == 0 && stick == 1);
  assert(r.add(0, Kind::Signal, "opened", "Opened") >= 0);
  const int openCommand = r.add(0, Kind::Command, "open", "Open door");
  entries[openCommand].ref.action = open;
  const int flash = r.add(0, Kind::Command, "flash", "Flash light");
  entries[flash].flags |= Testable;
  r.detailFor(flash)->testPin = 13;
  entries[r.add(0, Kind::Boolean, "open", "Open")].ref.boolean = &opened; // state may reuse a command ID
  const int number = r.add(1, Kind::Number, "progress", "Progress");
  entries[number].ref.number = &progress;
  *r.detailFor(number) = Detail{};
  r.detailFor(number)->max = 5;
  r.detailFor(number)->unit = "steps";
  const int state = r.add(1, Kind::Enum, "mode", "Mode");
  entries[state].ref.index = &mode;
  r.detailFor(state)->values = modes;
  r.detailFor(state)->valueCount = 3;
  entries[r.add(1, Kind::Boolean, "solved", "Solved")].ref.boolean = &solved;
  assert(r.add(1, Kind::Signal, "solved", "Solved") >= 0);
  assert(r.add(1, Kind::Command, "complete", "Complete") >= 0);
  props[1].completionSignal = "solved";
  props[1].completionCommand = "complete";

  assert(r.add(0, Kind::Signal, "opened", "Again") < 0); // duplicate within a group
  r.problem = nullptr;
  assert(r.addProp("exit-door", "Duplicate", nullptr) < 0);
  r.problem = nullptr;
  assert(r.addProp("Bad", "Bad", nullptr) < 0);
  r.problem = nullptr;
  assert(r.validate("Train station", "2.0.0") == nullptr);
  assert(r.validate("", "2.0.0") != nullptr);

  // Explicit UUIDs are normalized to lowercase; lookups ignore case.
  char id[37];
  r.propId(1, id);
  assert(std::string(id) == "0210a18f-c31f-45b7-a35a-e20eb82a6c11");
  assert(r.findProp("0210A18F-C31F-45B7-A35A-E20EB82A6C11") == 1);
  r.propId(0, id);
  assert(r.findProp(id) == 0);
  assert(r.findProp("00000000-0000-4000-8000-000000000000") < 0);
  assert(r.find(0, Kind::Command, "open") == openCommand);
  assert(r.find(0, Kind::Signal, "open") < 0);

  StringSink sink;
  r.writeDescription(sink, "Train \\ station", "2.0.0");
  assert(sink.text.size() == r.descriptionLength("Train \\ station", "2.0.0"));
  assert(sink.text.find("\"name\":\"Joystick \\\"sequence\\\"\"") != std::string::npos);
  assert(sink.text.find("\"testable\":true") != std::string::npos);
  assert(sink.text.find("\"min\":0,\"max\":5,\"unit\":\"steps\"") != std::string::npos);
  assert(sink.text.find("\"completion\":{\"signal\":\"solved\",\"command\":\"complete\"}") !=
         std::string::npos);
  if (argc > 1) {
    FILE *out = std::fopen(argv[1], "w");
    assert(out);
    std::fputs(sink.text.c_str(), out);
    std::fclose(out);
  }

  // Invalid completion references are caught once declarations are complete.
  props[0].completionSignal = "opened";
  props[0].completionCommand = "missing";
  assert(r.validate("Train station", "2.0.0") != nullptr);
  props[0].completionSignal = nullptr;

  // Scheduler: keyed replacement, cancel, Test scope and wraparound.
  Scheduler<3> s;
  assert(s.after(close, 5000, 1000, false));
  assert(s.after(close, 5000, 3000, false)); // second open reschedules the close
  s.runDue(6500);
  assert(closes == 0);
  s.runDue(8000);
  assert(closes == 1 && !s.pending(close));
  assert(s.after(open, 10, 0, true) && s.after(close, 10, 0, false));
  s.cancelTestScoped();
  s.runDue(20);
  assert(calls == 0 && closes == 2);
  assert(s.after(open, 10, 0xfffffff0u, false));
  s.runDue(0xfffffff8u);
  assert(calls == 0);
  s.runDue(5); // wrapped past the due time
  assert(calls == 1);
  assert(s.after(open, 1, 0, false) && s.after(close, 1, 0, false));
  s.cancelAll();
  s.runDue(100);
  assert(calls == 1 && closes == 2);
  std::puts("roomRegistry passed");
  return 0;
}
