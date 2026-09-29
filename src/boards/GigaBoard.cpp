// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#if defined(ARDUINO_GIGA)
#include "../BoardAdapter.h"
#include "PairingSlots.h"
#include <WiFi.h>
#include <mbed.h>
#include <utility>
#include <kvstore_global_api.h>
#include "GigaTls.h"

namespace {
constexpr size_t STORAGE_SIZE = 12288;
struct Record {
  uint32_t generation;
  uint32_t checksum;
  char json[STORAGE_SIZE];
};
// Storage scratch lives off the small Mbed loop-thread stack.
Record record;
uint32_t savedGeneration = 0;
int savedSlot = 0;
uint32_t checksum(const char *value) {
  uint32_t hash = 2166136261UL;
  while (*value) {
    hash ^= uint8_t(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

const char *storageKey(int slot) {
  return slot ? "/kv/ed-custom-1" : "/kv/ed-custom-0";
}

bool readRecord(int slot) {
  size_t actual = 0;
  return kv_get(storageKey(slot), &record, sizeof(record), &actual) == MBED_SUCCESS &&
         actual == sizeof(record) && memchr(record.json, 0, sizeof(record.json)) &&
         record.checksum == checksum(record.json);
}

bool savePairingRecord(JsonVariantConst value) {
  if (measureJson(value) >= STORAGE_SIZE)
    return false;
  record = {};
  record.generation = savedGeneration + 1;
  serializeJson(value, record.json, sizeof(record.json));
  record.checksum = checksum(record.json);
  int target = 1 - savedSlot;
  if (kv_set(storageKey(target), &record, sizeof(record), 0) != MBED_SUCCESS ||
      !readRecord(target) || record.generation != savedGeneration + 1)
    return false;
  savedSlot = target;
  savedGeneration = record.generation;
  return true;
}

class GigaBoard : public ed::BoardAdapter {
  StationTlsClient tls;
  WiFiUDP discovery;
  mbed::Ticker timer;

public:
  const char *id() const override { return "giga-r1-wifi"; }
  void begin() override { Serial.begin(115200); }
  Stream &setupStream() override { return Serial; }
  Client &secureClient() override { return tls; }
  void trustStation(const char *certificate) override { tls.setCACert(certificate); }
  bool networkConnected() override { return WiFi.status() == WL_CONNECTED; }
  bool joinNetwork(const char *ssid, const char *password) override {
    return (strlen(password) ? WiFi.begin(ssid, password) : WiFi.begin(ssid)) == WL_CONNECTED;
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
  bool loadPairing(JsonDocument &value) override {
    value.clear();
    savedGeneration = 0;
    uint32_t generations[2] = {};
    for (int slot = 0; slot < 2; ++slot)
      if (readRecord(slot))
        generations[slot] = record.generation;
    const int loaded = ed::detail::loadPairingSlot(generations, [&](int slot) {
      if (!readRecord(slot))
        return ed::detail::PairingRead::Invalid;
      value.clear();
      // Copy strings: the storage scratch buffer is reused on the next save.
      const auto error = deserializeJson(value, static_cast<const char *>(record.json));
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
  bool savePairing(JsonVariantConst value) override { return savePairingRecord(value); }
  bool startTimer(void (*tick)(), uint32_t periodMs) override {
    timer.attach(tick, std::chrono::milliseconds(periodMs));
    return true;
  }
};
} // namespace

namespace ed {
namespace detail {
BoardAdapter &defaultBoard() {
  static GigaBoard board;
  return board;
}
} // namespace detail
} // namespace ed
#endif
