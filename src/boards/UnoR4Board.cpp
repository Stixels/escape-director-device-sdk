// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#if defined(ARDUINO_UNOR4_WIFI)
#include "../BoardAdapter.h"
#include "PairingSlots.h"
#include <EEPROM.h>
#include <FspTimer.h>
#include <WiFiS3.h>

namespace {
// Two 4 KB slots fill the 8 KB data flash. Custom records use their own magic,
// so managed firmware ignores them (and vice versa); switching firmware kinds
// needs pairing again. Records stream to and from flash without a RAM buffer.
constexpr size_t SLOT_BYTES = 4096, HEADER_BYTES = 16;
constexpr uint32_t MAGIC = 0x45444331; // "EDC1"
struct Header {
  uint32_t magic, generation, length, checksum;
};
uint32_t savedGeneration = 0;
int savedSlot = 0;

uint32_t fnv(uint32_t hash, uint8_t value) {
  return (hash ^ value) * 16777619UL;
}

// EEPROM.update erases and reprograms a whole 1 KB flash block for every
// changed byte. Fill the core's block buffer instead and program each block
// once, as EEPROM.put does, without a second copy of the record in RAM.
class SlotWriter : public Print {
public:
  explicit SlotWriter(int slot) : base(slot * SLOT_BYTES + HEADER_BYTES) {}
  size_t write(uint8_t value) override {
    if (failed || length >= SLOT_BYTES - HEADER_BYTES)
      return 0;
    const uint32_t at = base + length;
    if (!blockOpen) {
      uint32_t remaining = 0;
      if (veeprom::getInstance().read_block(at, remaining) != ReadStatus::ALLOCATED) {
        failed = true;
        return 0;
      }
      blockEnd = at + remaining;
      blockOpen = true;
    }
    veeprom::getInstance().write_byte(value, at);
    ++length;
    checksum = fnv(checksum, value);
    if (at + 1 == blockEnd)
      commit();
    return 1;
  }
  bool commit() {
    if (blockOpen) {
      blockOpen = false;
      if (!veeprom::getInstance().write())
        failed = true;
    }
    return !failed;
  }
  size_t base, length = 0;
  uint32_t checksum = 2166136261UL;

private:
  uint32_t blockEnd = 0;
  bool blockOpen = false, failed = false;
};

class SlotReader {
public:
  SlotReader(int slot, size_t length) : at(slot * SLOT_BYTES + HEADER_BYTES), end(at + length) {}
  int read() { return at < end ? EEPROM.read(at++) : -1; }
  size_t readBytes(char *buffer, size_t count) {
    size_t copied = 0;
    for (int value; copied < count && (value = read()) >= 0; copied++)
      buffer[copied] = char(value);
    return copied;
  }

private:
  size_t at, end;
};

bool readHeader(int slot, Header &header) {
  EEPROM.get(slot * SLOT_BYTES, header);
  if (header.magic != MAGIC || header.length == 0 || header.length > SLOT_BYTES - HEADER_BYTES)
    return false;
  uint32_t hash = 2166136261UL;
  SlotReader reader(slot, header.length);
  for (int value; (value = reader.read()) >= 0;)
    hash = fnv(hash, uint8_t(value));
  return hash == header.checksum;
}

FspTimer timer;
void (*timerTick)() = nullptr;
void onTimer(timer_callback_args_t *) {
  if (timerTick)
    timerTick();
}

class UnoR4Board : public ed::BoardAdapter {
  WiFiSSLClient tls;
  WiFiUDP discovery;
  // certificateOnly parses just the station certificate, for connection attempts.
  bool load(JsonDocument &value, bool certificateOnly) {
    value.clear();
    savedGeneration = 0;
    if (EEPROM.length() < 2 * SLOT_BYTES)
      return false;
    Header headers[2] = {};
    uint32_t generations[2] = {};
    for (int slot = 0; slot < 2; ++slot)
      if (readHeader(slot, headers[slot]))
        generations[slot] = headers[slot].generation;
    const int loaded = ed::detail::loadPairingSlot(generations, [&](int slot) {
      value.clear();
      SlotReader reader(slot, headers[slot].length);
      JsonDocument filter;
      if (certificateOnly)
        filter["connection"]["certificate"] = true;
      const auto error = certificateOnly
                             ? deserializeJson(value, reader, DeserializationOption::Filter(filter))
                             : deserializeJson(value, reader);
      return !error ? ed::detail::PairingRead::Loaded
             : error == DeserializationError::NoMemory ? ed::detail::PairingRead::Unavailable
                                                       : ed::detail::PairingRead::Invalid;
    });
    if (loaded < 0) {
      if (loaded == ed::detail::PAIRING_UNAVAILABLE)
        Serial.println(F("Escape Director: not enough memory to load pairing; restart the controller."));
      value.clear();
      return false;
    }
    savedSlot = loaded;
    savedGeneration = generations[loaded];
    return true;
  }

public:
  const char *id() const override { return "uno-r4-wifi"; }
  void begin() override {
    Serial.begin(115200);
    WiFi.setTimeout(15000);
    tls.setTimeout(2000);
#ifndef ED_TLS_CONNECT_TIMEOUT_MS
#define ED_TLS_CONNECT_TIMEOUT_MS 5000
#endif
    tls.setConnectionTimeout(ED_TLS_CONNECT_TIMEOUT_MS);
  }
  Stream &setupStream() override { return Serial; }
  Client &secureClient() override { return tls; }
  void trustStation(const char *certificate) override { tls.setCACert(certificate); }
  // Association can finish before DHCP; only an address is a usable connection.
  bool networkConnected() override {
    return WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  }
  // WiFi.begin() sends the join command, then waits on this board for up to
  // its timeout. A zero timeout returns once the command is sent.
  void startJoin(const char *ssid, const char *password) override {
    WiFi.setTimeout(0);
    if (strlen(password))
      WiFi.begin(ssid, password);
    else
      WiFi.begin(ssid);
    WiFi.setTimeout(15000);
  }
  bool joinNetwork(const char *ssid, const char *password) override {
    if ((strlen(password) ? WiFi.begin(ssid, password) : WiFi.begin(ssid)) != WL_CONNECTED)
      return false;
    for (uint32_t start = millis(); uint32_t(WiFi.localIP()) == 0;) {
      if (millis() - start > 10000)
        return false;
      delay(50);
    }
    return true;
  }
  void disconnectNetwork() override { WiFi.disconnect(); }
  IPAddress localIP() override { return WiFi.localIP(); }
  IPAddress subnetMask() override { return WiFi.subnetMask(); }
  UDP &discoverySocket() override { return discovery; }
  bool scanNetworks(JsonArray networks) override {
    int count = WiFi.scanNetworks();
    if (count < 0)
      return false;
    for (int i = 0; i < count && networks.size() < 32; i++) {
      const char *ssid = WiFi.SSID(i);
      if (!ssid || !strlen(ssid))
        continue;
      auto network = networks.add<JsonObject>();
      network["ssid"] = ssid;
      network["rssi"] = WiFi.RSSI(i);
      network["secured"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    }
    return true;
  }
  bool loadPairing(JsonDocument &value) override { return load(value, false); }
  bool loadCertificate(JsonDocument &value) override { return load(value, true); }
  bool savePairing(JsonVariantConst value) override {
    if (EEPROM.length() < 2 * SLOT_BYTES || measureJson(value) >= SLOT_BYTES - HEADER_BYTES)
      return false;
    // The core allocates each 1 KB flash block's buffer with new, which aborts
    // instead of failing when the heap is short. Refuse the save while it can.
    if (void *room = malloc(1024 + 64))
      free(room);
    else
      return false;
    // Write the idle slot, then verify it; the active slot stays usable on failure.
    const int target = savedGeneration ? 1 - savedSlot : 0;
    SlotWriter writer(target);
    serializeJson(value, writer);
    if (!writer.commit() || writer.length != measureJson(value))
      return false;
    Header header = {MAGIC, savedGeneration + 1, uint32_t(writer.length), writer.checksum};
    EEPROM.put(target * SLOT_BYTES, header);
    Header check;
    if (!readHeader(target, check) || check.generation != header.generation)
      return false;
    savedSlot = target;
    savedGeneration = header.generation;
    return true;
  }
  bool startTimer(void (*tick)(), uint32_t periodMs) override {
    uint8_t type;
    const int8_t index = FspTimer::get_available_timer(type);
    timerTick = tick;
    return index >= 0 &&
           timer.begin(TIMER_MODE_PERIODIC, type, index, 1000.0f / periodMs, 0.0f, onTimer) &&
           timer.setup_overflow_irq() && timer.open() && timer.start();
  }
};
} // namespace

namespace ed {
namespace detail {
BoardAdapter &defaultBoard() {
  static UnoR4Board board;
  return board;
}
} // namespace detail
} // namespace ed
#endif
