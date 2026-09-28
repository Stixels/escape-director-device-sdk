// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include "EscapeDirector.h"
#include "CompactAllocation.h"
#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include <EscapeDirectorDevice.h>
#include <memory>
#include <atomic>
#include <utility>

namespace {
ed::BoardAdapter *board = nullptr;
std::unique_ptr<MqttClient> mqtt;
ed::CustomDriver *driver = nullptr;
// Reclaim ArduinoJson string growth slack on newlib-nano without losing
// malloc's alignment. The allocation helper is also exercised by host tests.
class CompactAllocator : public ArduinoJson::Allocator {
public:
  void *allocate(size_t size) override { return ed::detail::compactAllocate(size); }
  void deallocate(void *pointer) override { ed::detail::compactFree(pointer); }
  void *reallocate(void *pointer, size_t size) override {
    return ed::detail::compactReallocate(pointer, size);
  }
} compactAllocator;

// manifest keeps only the IDs the client checks; the full description stays in flash.
const char *description = nullptr;
size_t descriptionBytes = 0;
// Long-lived documents, and the setup request that can become the saved record.
JsonDocument manifest(&compactAllocator), saved(&compactAllocator),
    pendingNetwork(&compactAllocator);
ed::NetworkRetry retry;
ed::CommandWindow<> commands;
String session, game, test, discoveredHost, discoveryNonce;
std::atomic<uint32_t> leaseUntil{0}, activeGame{0};
uint32_t generation = 0, reportAt = 0, discoveryAt = 0;
size_t stationAddressAttempt = 0;
ed::StationClock stationClock;
bool ready = false, discoveryStarted = false, discardSerialLine = false;
int64_t now() {
  return stationClock.now(millis());
}

String uuid() {
  char value[37];
  snprintf(value, sizeof(value), "%08lx-%04lx-4%03lx-8%03lx-%04lx%08lx",
           (unsigned long)random(0x7fffffff), (unsigned long)random(65536),
           (unsigned long)random(4096), (unsigned long)random(4096), (unsigned long)random(65536),
           (unsigned long)random(0x7fffffff));
  return String(value);
}

void clearAuthority() {
  activeGame = 0;
  bool wasTesting = !test.isEmpty();
  game = "";
  test = "";
  leaseUntil = 0;
  if (driver && wasTesting) {
    driver->testLease(0);
    driver->endTest();
  }
}

void detach() {
  ready = false;
  session = "";
  clearAuthority();
}

void tick() {
  if (leaseUntil && int32_t(millis() - leaseUntil) >= 0)
    clearAuthority();
}

// Writes in small chunks: some network clients send each write() as its own
// packet, and a whole-message buffer would cost as much RAM as the message.
class ChunkedWriter : public Print {
public:
  explicit ChunkedWriter(Print &out) : out(out) {}
  size_t write(uint8_t value) override {
    buffer[used++] = value;
    if (used == sizeof(buffer))
      drain();
    return 1;
  }
  void drain() {
    if (used && out.write(buffer, used) != used)
      failed = true;
    used = 0;
  }
  bool ok() const { return !failed; }

private:
  Print &out;
  uint8_t buffer[128];
  size_t used = 0;
  bool failed = false;
};

// Drops the first byte: the opening brace of a message spliced after the description.
class SkipFirst : public Print {
public:
  explicit SkipFirst(Print &out) : out(out) {}
  size_t write(uint8_t value) override {
    if (skipped)
      return out.write(value);
    skipped = true;
    return 1;
  }

private:
  Print &out;
  bool skipped = false;
};

// {"description":<compact description>,<message fields>} without copying the
// description into RAM.
constexpr char DESCRIPTION_KEY[] = "{\"description\":";
size_t describedLength(JsonDocument &message) {
  return strlen(DESCRIPTION_KEY) + descriptionBytes + measureJson(message);
}
void writeDescribed(JsonDocument &message, Print &out) {
  out.print(DESCRIPTION_KEY);
  ed::forEachCompactJson(description, [&](char c) { out.write(uint8_t(c)); });
  out.write(',');
  SkipFirst rest(out);
  serializeJson(message, rest);
}

void publish(JsonDocument &message, bool withDescription = false) {
  if (!mqtt->connected() || session.isEmpty())
    return;
  message["sessionId"] = session;
  const String type = message["type"] | "";
  // Clock synchronization is shared transport metadata, not a prop event.
  if (type != "custom-hello" && type != "clock-sync")
    message["descriptionId"] = saved["connection"]["descriptionId"];
  const size_t length = withDescription ? describedLength(message) : measureJson(message);
  String topic = "ed/v1/" + saved["connection"]["controllerId"].as<String>() + "/up";
  if (!mqtt->beginMessage(topic, (unsigned long)length, false, 0))
    return;
  ChunkedWriter out(*mqtt);
  if (withDescription)
    writeDescribed(message, out);
  else
    serializeJson(message, out);
  out.drain();
  if (!out.ok() || !mqtt->endMessage()) {
    mqtt->stop();
    detach();
  }
}

void report() {
  if (!ready)
    return;
  JsonDocument message;
  message["type"] = "custom-state";
  driver->state(message["state"].to<JsonObject>());
  publish(message);
  reportAt = millis();
}

void lease(uint32_t remaining) {
  leaseUntil = millis() + remaining;
  if (!test.isEmpty())
    driver->testLease(leaseUntil);
}

bool declared(const char *propId, const char *capability, const char *kind, bool testing = false) {
  for (JsonObject prop : manifest["props"].as<JsonArray>())
    if (prop["id"].as<String>() == propId)
      for (JsonObject item : prop[kind].as<JsonArray>())
        if (item["id"].as<String>() == capability)
          return !testing || item["testable"].as<bool>();
  return false;
}

const char *execute(JsonDocument &message) {
  tick();
  int64_t expires = message["expiresAt"] | int64_t(0), time = now();
  if (expires <= time || expires > time + 3000)
    return "expired_command";
  JsonObject op = message["operation"];
  String type = op["type"] | "";
  if (type != "lease") {
    const char *id = op["commandId"] | message["requestId"].as<const char *>();
    auto admitted = commands.admit(id, expires, time);
    if (admitted != ed::CommandWindow<>::Admission::Accepted)
      return admitted == ed::CommandWindow<>::Admission::Duplicate ? "confirmation_unknown"
                                                                   : "command_limit";
  }
  uint32_t remaining = uint32_t(expires - time);
  driver->commandDeadline(millis() + remaining);
  if (type == "lease") {
    if ((game.isEmpty() && test.isEmpty()) || (op["gameId"] | String("")) != game ||
        (op["testId"] | String("")) != test)
      return "stale_context";
    lease(remaining);
  } else if (type == "start") {
    if (!game.isEmpty() || !test.isEmpty())
      return "room_in_use";
    game = op["gameId"].as<String>();
    lease(remaining);
    activeGame = ++generation;
  } else if (type == "end") {
    if (game.isEmpty() || op["gameId"].as<String>() != game)
      return "stale_game";
    clearAuthority();
  } else if (type == "test-mode") {
    if (!game.isEmpty())
      return "finish_game_first";
    if (op["enabled"] == true) {
      if (!test.isEmpty())
        return "test_mode_in_use";
      test = op["testId"].as<String>();
      lease(remaining);
    } else {
      if (test.isEmpty() || op["testId"].as<String>() != test)
        return "stale_test";
      clearAuthority();
    }
  } else if (type == "reset-room") {
    if (!game.isEmpty() || !test.isEmpty())
      return "room_in_use";
    driver->resetRoom();
  } else if (type == "command" || type == "test-command") {
    bool testing = type == "test-command";
    if (testing ? test.isEmpty() || op["testId"].as<String>() != test
                : game.isEmpty() || op["gameId"].as<String>() != game)
      return "stale_context";
    const char *propId = op["propId"] | "", *capability = op["capability"] | "";
    if (!declared(propId, capability, "commands", testing))
      return "unsupported_command";
    if (!driver->command(propId, capability, testing))
      return "driver_failed";
  } else
    return "unsupported_command";
  return nullptr;
}

void receiveMqtt(int length) {
  if (length > 32768 || mqtt->messageRetain()) {
    while (mqtt->available())
      mqtt->read();
    return;
  }
  JsonDocument message;
  if (deserializeJson(message, *mqtt))
    return;
  String type = message["type"] | "";
  if (type == "welcome") {
    detach();
    commands.clear();
    if (message["customProtocolVersion"].as<int>() != 1 ||
        message["authorityVersion"].as<uint32_t>() != saved["connection"]["authorityVersion"].as<uint32_t>() ||
        message["descriptionId"].as<String>() != saved["connection"]["descriptionId"].as<String>()) {
      mqtt->stop();
      return;
    }
    session = message["sessionId"].as<String>();
    stationClock.begin(message["now"].as<int64_t>(), millis());
    JsonDocument hello;
    hello["type"] = "custom-hello";
    hello["protocolVersion"] = 1;
    publish(hello, true);
    return;
  }
  if (session.isEmpty() || message["sessionId"].as<String>() != session)
    return;
  if (type == "clock-sync") {
    if (message["authorityVersion"].as<uint32_t>() == saved["connection"]["authorityVersion"].as<uint32_t>() &&
        message["sequence"].is<uint32_t>() && message["now"].is<int64_t>())
      stationClock.receive(message["sequence"], message["now"], millis());
    return;
  }
  if (message["descriptionId"].as<String>() != saved["connection"]["descriptionId"].as<String>())
    return;
  if (type == "custom-ready") {
    ready = true;
    report();
    return;
  }
  if (!ready || type != "custom-request" ||
      message["authorityVersion"].as<uint32_t>() != saved["connection"]["authorityVersion"].as<uint32_t>())
    return;
  const char *error = execute(message);
  JsonDocument result;
  result["type"] = "result";
  result["requestId"] = message["requestId"];
  result["outcome"] = error ? "rejected" : "completed";
  if (error)
    result["code"] = error;
  publish(result);
  report();
}

void receiveSerial() {
  Stream &usb = board->setupStream();
  while (usb.available()) {
    if (discardSerialLine || usb.peek() == '\n' || usb.peek() == '\r') {
      if (usb.read() == '\n')
        discardSerialLine = false;
      continue;
    }
    // Parse straight from USB: holding the line and its document at once
    // exhausts a 32 KB board while provisioning a certificate.
    JsonDocument message(&compactAllocator);
    SerialJsonLine<Stream> line(usb);
    auto invalid = deserializeJson(message, line);
    // Finish the frame even after an error so its tail is not read as a request.
    discardSerialLine = !line.complete();
    const uint32_t drainUntil = millis() + 1000;
    while (discardSerialLine && int32_t(millis() - drainUntil) < 0)
      if (usb.available() && usb.read() == '\n')
        discardSerialLine = false;
    if (invalid) {
      // Answer when the request is identifiable so setup fails with a reason
      // instead of a timeout; the request itself is not acted on.
      if (message["requestId"].is<const char *>()) {
        JsonDocument rejected;
        rejected["type"] = "error";
        rejected["requestId"] = message["requestId"];
        rejected["code"] = invalid == DeserializationError::NoMemory ? "out_of_memory"
                                                                      : "invalid_message";
        serializeJson(rejected, usb);
        usb.println();
      }
      continue;
    }
    JsonDocument response;
    response["requestId"] = message["requestId"];
    auto reply = [&]() {
      ChunkedWriter out(usb);
      serializeJson(response, out);
      out.drain();
      usb.println();
    };
    auto reject = [&](const char *code) {
      response["type"] = "error";
      response["code"] = code;
      reply();
    };
    String type = message["type"] | "";
    if (type == "identify") {
      response["type"] = "controller";
      response["product"] = "escape-director";
      response["protocolVersion"] = 2;
      response["board"] = board->id();
      response["firmwareVersion"] = manifest["firmwareVersion"];
      response["firmwareKind"] = "custom";
      response["deviceId"] = saved["connection"]["deviceId"];
      response["controllerId"] = saved["connection"]["controllerId"];
      ChunkedWriter out(usb);
      writeDescribed(response, out);
      out.drain();
      usb.println();
      continue;
    }
    if (message["protocolVersion"].as<int>() != 2)
      continue;
    if (type == "setup-status") {
      response["type"] = type;
      response["wifiConnected"] = board->networkConnected();
      response["stationConnected"] = ready && mqtt->connected();
      response["deviceId"] = saved["connection"]["deviceId"];
      reply();
      continue;
    }
    if (!game.isEmpty() || !test.isEmpty()) {
      reject("device_busy");
      continue;
    }
    if (type == "scan-networks") {
      response["type"] = "networks";
      if (!board->scanNetworks(response["networks"].to<JsonArray>())) {
        reject("scan_failed");
        continue;
      }
      reply();
    } else if (type == "join-network") {
      if (!message["network"]["ssid"].is<const char *>() ||
          !message["network"]["password"].is<const char *>()) {
        reject("invalid_network");
        continue;
      }
      mqtt->stop();
      detach();
      pendingNetwork.set(message["network"]);
      board->disconnectNetwork();
      const char *password = pendingNetwork["password"];
      if (!board->joinNetwork(pendingNetwork["ssid"], password)) {
        pendingNetwork.clear();
        reject("network_failed");
        continue;
      }
      response["type"] = "network-joined";
      reply();
    } else if (type == "provision" || type == "save-network") {
      // A same-controller sketch update can retain its saved Wi-Fi details.
      // Initial pairing and network changes still require a successful join.
      if (type == "provision" && pendingNetwork.isNull() &&
          !saved["network"].isNull() &&
          message["connection"]["controllerId"].as<String>() == saved["connection"]["controllerId"].as<String>() &&
          message["connection"]["deviceId"].as<String>() == saved["connection"]["deviceId"].as<String>())
        pendingNetwork.set(saved["network"]);
      if (pendingNetwork.isNull()) {
        reject("network_failed");
        continue;
      }
      // Build the new record in place instead of copying: the certificate is
      // the largest value and a 32 KB board cannot hold it twice. Provisioning
      // reuses the parsed request; a network change edits the saved record and
      // reloads the last durable one if saving fails.
      const bool provisioning = type == "provision";
      JsonDocument &next = provisioning ? message : saved;
      if (provisioning) {
        JsonObject connection = message["connection"];
        if (!connection["descriptionId"].is<const char *>() ||
            strlen(connection["descriptionId"]) != 64 ||
            !connection["certificate"].is<const char *>() ||
            !connection["host"].is<const char *>() || !connection["password"].is<const char *>() ||
            !connection["controllerId"].is<const char *>() ||
            !connection["deviceId"].is<const char *>()) {
          reject("invalid_connection");
          continue;
        }
        message.remove("type");
        message.remove("requestId");
        message.remove("protocolVersion");
      } else if (saved["connection"].isNull()) {
        reject("invalid_connection");
        continue;
      }
      next["network"] = pendingNetwork;
      if (next.overflowed() || !board->savePairing(next.as<JsonVariantConst>())) {
        if (!provisioning)
          board->loadPairing(saved);
        reject("storage_failed");
        continue;
      }
      if (provisioning)
        saved = std::move(message);
      pendingNetwork.clear();
      retry.reset();
      mqtt->stop();
      detach();
      response["type"] = "provisioned";
      response["deviceId"] = saved["connection"]["deviceId"];
      reply();
    } else
      reject("invalid_message");
  }
}

void discover() {
  if (!board->networkConnected()) {
    if (discoveryStarted)
      board->discoverySocket().stop();
    discoveryStarted = false;
    discoveredHost = "";
    return;
  }
  if (saved["connection"].isNull() || mqtt->connected())
    return;
  // Bind only after the adapter has a network interface.
  // Recreate it after reconnect so station discovery survives a network change.
  if (!discoveryStarted)
    discoveryStarted = board->discoverySocket().begin(43126);
  if (!discoveryStarted)
    return;
  int size = board->discoverySocket().parsePacket();
  if (size > 0 && size <= 256) {
    // Bulk reads also avoid single-byte UDP quirks in some Arduino cores.
    char bytes[257];
    int count = board->discoverySocket().read(bytes, 256);
    if (count <= 0)
      return;
    bytes[count] = 0;
    JsonDocument response;
    if (!deserializeJson(response, bytes) && response["type"] == "room-station" &&
        response["nonce"].as<String>() == discoveryNonce &&
        response["port"] == saved["connection"]["port"] &&
        response["controllerId"] == saved["connection"]["controllerId"])
      discoveredHost = board->discoverySocket().remoteIP().toString();
  } else if (size > 0)
    board->discoverySocket().flush();
  if (uint32_t(millis() - discoveryAt) < 3000)
    return;
  discoveryAt = millis();
  discoveryNonce = uuid();
  discoveryNonce.replace("-", "");
  IPAddress ip = board->localIP(), mask = board->subnetMask(),
            broadcast(ip[0] | ~mask[0], ip[1] | ~mask[1], ip[2] | ~mask[2], ip[3] | ~mask[3]);
  JsonDocument request;
  request["type"] = "find-room-station";
  request["controllerId"] = saved["connection"]["controllerId"];
  request["nonce"] = discoveryNonce;
  board->discoverySocket().beginPacket(broadcast, 43125);
  serializeJson(request, board->discoverySocket());
  board->discoverySocket().endPacket();
}
} // namespace
namespace ed {
void begin(const char *descriptionJson, CustomDriver &implementation, BoardAdapter &adapter) {
  board = &adapter;
  board->begin();
  mqtt.reset(new MqttClient(board->secureClient()));
  driver = &implementation;
  description = descriptionJson;
  descriptionBytes = ed::compactJsonLength(descriptionJson);
  {
    // Commands and signals are checked by ID; the app and Connector validate the rest.
    JsonDocument filter;
    filter["firmwareVersion"] = true;
    JsonObject prop = filter["props"].add<JsonObject>();
    prop["id"] = true;
    prop["signals"].add<JsonObject>()["id"] = true;
    JsonObject command = prop["commands"].add<JsonObject>();
    command["id"] = true;
    command["testable"] = true;
    deserializeJson(manifest, descriptionJson, DeserializationOption::Filter(filter));
  }
  // Non-JSON lines are ignored by setup and visible in a serial monitor.
  if (descriptionBytes > 16384) {
    board->setupStream().print(F("Escape Director: description is "));
    board->setupStream().print(descriptionBytes);
    board->setupStream().println(F(" bytes; the limit is 16384."));
  }
  board->loadPairing(saved);
  mqtt->setConnectionTimeout(1500);
  mqtt->setKeepAliveInterval(20000);
  mqtt->onMessage(receiveMqtt);
}

void poll() {
  tick();
  receiveSerial();
  if (!pendingNetwork.isNull())
    return;
  retry.observe(mqtt->connected(), millis());
  discover();
  if (!mqtt->connected()) {
    detach();
    if (saved["connection"].isNull() || !retry.ready(millis()))
      return;
    retry.attempted(millis());
    if (!board->networkConnected()) {
      board->joinNetwork(saved["network"]["ssid"], saved["network"]["password"]);
    }
    if (!board->networkConnected())
      return;
    board->trustStation(saved["connection"]["certificate"]);
    mqtt->setId(saved["connection"]["controllerId"].as<const char *>());
    mqtt->setUsernamePassword(saved["connection"]["controllerId"].as<const char *>(), saved["connection"]["password"].as<const char *>());
    JsonArray addresses = saved["connection"]["addresses"].as<JsonArray>();
    String host = saved["connection"]["host"].as<String>();
    if (!discoveredHost.isEmpty()) {
      host = discoveredHost;
      discoveredHost = "";
    } else if (addresses.size()) {
      IPAddress local = board->localIP(), mask = board->subnetMask();
      const size_t index = ed::stationAddressIndex(
          stationAddressAttempt++, addresses.size(), [&](size_t candidate) {
            IPAddress address;
            return address.fromString(addresses[candidate].as<const char *>()) &&
                   (uint32_t(address) & uint32_t(mask)) ==
                       (uint32_t(local) & uint32_t(mask));
          });
      host = addresses[index].as<String>();
    }
    if (mqtt->connect(host.c_str(), saved["connection"]["port"].as<int>())) {
      stationAddressAttempt = 0;
      String topic = "ed/v1/" + saved["connection"]["controllerId"].as<String>() + "/down";
      mqtt->subscribe(topic);
    }
  }
  mqtt->poll();
  tick();
  if (ready && mqtt->connected()) {
    if (const uint32_t sequence = stationClock.request(millis())) {
      JsonDocument request;
      request["type"] = "clock-sync";
      request["sequence"] = sequence;
      publish(request);
    }
    if (uint32_t(millis() - reportAt) >= 500)
      report();
  }
}

bool startTimer(void (*tick)(), uint32_t periodMs) {
  return board && tick && periodMs && board->startTimer(tick, periodMs);
}

uint32_t captureGame() {
  const uint32_t captured = activeGame.load();
  return captured && int32_t(millis() - leaseUntil.load()) < 0 &&
                 captured == activeGame.load() ? captured : 0;
}

bool signal(const char *propId, const char *capability, uint32_t capturedGame,
            uint32_t capturedAtMs) {
  tick();
  if (!capturedGame || capturedGame != activeGame || uint32_t(millis() - capturedAtMs) > 2000 ||
      !ready || game.isEmpty() || !mqtt->connected() || !declared(propId, capability, "signals"))
    return false;
  JsonDocument message;
  message["type"] = "custom-signal";
  message["eventId"] = uuid();
  message["gameId"] = game;
  message["propId"] = propId;
  message["capability"] = capability;
  message["occurredAt"] = now();
  message["expiresAt"] = now() + 2000;
  publish(message);
  return true;
}
} // namespace ed
