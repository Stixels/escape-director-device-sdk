// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT
#pragma once
// PRIVATE PROTOTYPE. Not included in the distributable package or supported API.
#include "EscapeDirector.h"
#include "BackgroundMailbox.h"
#include <string.h>
#if defined(ARDUINO_UNOR4_WIFI)
#include <Arduino_FreeRTOS.h>
AUTOSTART_FREERTOS
// newlib-nano's heap is shared by the sketch and network task. Its hooks must
// serialize allocator metadata; never perform I/O from inside these hooks.
extern "C" void __malloc_lock(struct _reent *) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) taskENTER_CRITICAL();
}
extern "C" void __malloc_unlock(struct _reent *) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) taskEXIT_CRITICAL();
}
#elif defined(ARDUINO_GIGA)
#include <mbed.h>
#else
#error "Background prototype supports UNO R4 WiFi and GIGA only"
#endif

namespace ed { namespace experimental {
// All user callbacks execute in poll() on the sketch task. Return promptly;
// normal loop code also remains responsible for avoiding its own blocking waits.
class Driver {
public:
  virtual void state(JsonObject) = 0;
  virtual bool command(const char *, const char *, bool testing) = 0;
  virtual void resetRoom() = 0;
  virtual void endTest() = 0;
  virtual ~Driver() = default;
};
class BackgroundClient : private ed::CustomDriver {
  enum Kind { State, Command, Reset, EndTest };
  struct Call {
    Kind kind;
    JsonObject report;
    const char *prop = nullptr, *capability = nullptr;
    bool testing = false;
    uint32_t deadline = 0, game = 0;
  };
  struct Event { char prop[37], capability[81]; uint32_t game, at; };
  Driver &driver;
  Request<Call> request;
  Queue<Event, 4> events;
  std::atomic<uint32_t> testUntil{0};
  uint32_t dispatchUntil = 0;
  bool started = false;
#ifdef ED_BACKGROUND_BENCH
  std::atomic<uint32_t> loopCount{0}, maxLoopGap{0};
  uint32_t lastLoop = 0;
  std::atomic<uint32_t> sketchStackWords{UINT32_MAX};
#endif
#if defined(ARDUINO_GIGA)
  rtos::Thread worker{osPriorityBelowNormal, 4096, nullptr, "ed-network"};
#endif
  static void pause() {
#if defined(ARDUINO_UNOR4_WIFI)
    vTaskDelay(1);
#else
    rtos::ThisThread::sleep_for(std::chrono::milliseconds(1));
#endif
  }
  bool invoke(Call call) {
    request.post(call);
    while (!request.finished()) pause();
    return request.release();
  }
  void state(JsonObject out) override { invoke({State, out}); }
  bool command(const char *prop, const char *capability, bool testing) override {
    return invoke({Command, JsonObject(), prop, capability, testing, dispatchUntil,
                   testing ? 0 : ed::captureGame()});
  }
  void resetRoom() override { invoke({Reset, JsonObject(), nullptr, nullptr, false, dispatchUntil}); }
  void endTest() override {
    // Cleanup is executed locally even if the network task becomes stuck later.
    testUntil.store(0);
    invoke({EndTest, JsonObject()});
  }
  void testLease(uint32_t deadline) override {
    testUntil.store(deadline);
  }
  void commandDeadline(uint32_t deadline) override { dispatchUntil = deadline; }
  static void run(void *context) {
    auto &self = *static_cast<BackgroundClient *>(context);
#ifdef ED_BACKGROUND_BENCH
    Serial.println("ED background worker started");
    uint32_t lastReport = 0;
#endif
    for (;;) {
      ed::poll();
      Event event;
      // Fixed queue capacity bounds work. Existing signal() rechecks game
      // generation, readiness and the two-second age limit before publishing.
      for (size_t i = 0; i < 4 && self.events.pop(event); ++i)
        ed::signal(event.prop, event.capability, event.game, event.at);
#ifdef ED_BACKGROUND_BENCH
      if (uint32_t(millis() - lastReport) >= 5000) {
        lastReport = millis();
        Serial.print("ED bench loops="); Serial.print(self.loopCount.load());
        Serial.print(" maxGapMs="); Serial.print(self.maxLoopGap.load());
#if defined(ARDUINO_UNOR4_WIFI)
        Serial.print(" rtosFree="); Serial.print(xPortGetFreeHeapSize());
#if INCLUDE_uxTaskGetStackHighWaterMark
        Serial.print(" workerStackMinBytes="); Serial.print(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
        Serial.print(" sketchStackMinBytes="); Serial.print(self.sketchStackWords.load() * sizeof(StackType_t));
#endif
#endif
        Serial.println();
      }
#endif
      pause();
    }
  }
public:
  explicit BackgroundClient(Driver &implementation) : driver(implementation) {}
  // Call once from setup. The object and description must have static lifetime.
  bool begin(const char *description) {
    if (started) return false;
    ed::begin(description, *this);
#if defined(ARDUINO_UNOR4_WIFI)
    // Core supplies a priority-4 sketch task and DISABLES time slicing. A
    // lower-priority worker plus the bounded pause in poll() is essential:
    // equal priorities would let a busy WiFi modem wait starve the sketch.
    TaskHandle_t task = nullptr;
    started = xTaskCreate(run, "ed-network", 512, this, 3, &task) == pdPASS;
    // Core autostart has not yet created its 4096-byte sketch stack, idle
    // and timer tasks/queue. Conservatively reserve their allocations. Refuse
    // background mode instead of leaving core startup without enough heap.
    constexpr size_t reserve = 4096 + 2 * 128 * sizeof(StackType_t) +
      3 * sizeof(StaticTask_t) + sizeof(StaticQueue_t) + 10 * 32 + 128;
    if (started && xPortGetFreeHeapSize() < reserve) {
      vTaskDelete(task);
      started = false;
    }
#else
    started = worker.start(mbed::callback(run, this)) == osOK;
#endif
    return started;
  }
  void poll() {
    if (!started) return;
#ifdef ED_BACKGROUND_BENCH
    const uint32_t current = millis();
    if (lastLoop) {
      const uint32_t gap = current - lastLoop;
      if (gap > maxLoopGap.load()) maxLoopGap.store(gap);
    }
    lastLoop = current;
    loopCount.fetch_add(1);
#if defined(ARDUINO_UNOR4_WIFI) && INCLUDE_uxTaskGetStackHighWaterMark
    const uint32_t unused = uxTaskGetStackHighWaterMark(nullptr);
    if (unused < sketchStackWords.load()) sketchStackWords.store(unused);
#endif
#endif
    uint32_t deadline = testUntil.load();
    if (deadline && expired(millis(), deadline) && testUntil.compare_exchange_strong(deadline, 0))
      driver.endTest();
    if (Call *call = request.take()) {
      bool accepted = true;
      switch (call->kind) {
      case State: driver.state(call->report); break;
      case EndTest: driver.endTest(); break;
      case Reset:
        accepted = !expired(millis(), call->deadline);
        if (accepted) driver.resetRoom();
        break;
      case Command:
        accepted = commandAllowed(millis(), call->deadline, call->testing,
                                  testUntil.load(), call->game, ed::captureGame());
        if (accepted) accepted = driver.command(call->prop, call->capability, call->testing);
        break;
      }
      request.complete(accepted);
    }
    // Yield one scheduler tick to the lower-priority networking worker.
    // This is a bounded scheduling interval, never a network-response wait.
    pause();
  }
  // Enqueued means accepted locally, not delivered. Never retry old edges.
  bool signal(const char *prop, const char *capability) {
    const uint32_t game = ed::captureGame();
    if (!started || !game || !prop || !capability || strlen(prop) != 36 || strlen(capability) > 80) return false;
    Event event{};
    strcpy(event.prop, prop); strcpy(event.capability, capability);
    event.game = game; event.at = millis();
    return events.push(event);
  }
};
} }
