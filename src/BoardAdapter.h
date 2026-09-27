// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>
#include <Udp.h>

namespace ed {

// Platform services only. MQTT, pairing messages and Game/Test rules live in
// the shared client. Adapter objects and their streams must live for the entire sketch.
class BoardAdapter {
public:
  virtual ~BoardAdapter() = default;
  virtual const char *id() const = 0;
  virtual void begin() = 0;
  virtual Stream &setupStream() = 0;

  // The Client must validate the pinned CA and station identity, never insecure TLS.
  // The certificate stays alive until the connection is stopped/reprovisioned.
  virtual Client &secureClient() = 0;
  virtual void trustStation(const char *certificate) = 0;
  virtual bool networkConnected() = 0;
  virtual bool joinNetwork(const char *ssid, const char *password) = 0;
  virtual void disconnectNetwork() = 0;
  // At most 32 entries: {ssid, rssi, secured}. Omit empty/hidden SSIDs.
  virtual bool scanNetworks(JsonArray networks) = 0;
  virtual IPAddress localIP() = 0;
  virtual IPAddress subnetMask() = 0;
  virtual UDP &discoverySocket() = 0;

  // Missing/corrupt records leave value empty. Save must preserve the previous
  // usable record on failure and report success only after durable readback.
  virtual bool loadPairing(JsonDocument &value) = 0;
  virtual bool savePairing(JsonVariantConst value) = 0;

  // Calls tick from a hardware timer every periodMs, independent of blocking
  // network calls. Sketches use it through ed::startTimer for inputs and deadlines.
  virtual bool startTimer(void (*tick)(), uint32_t periodMs) = 0;
};

} // namespace ed
