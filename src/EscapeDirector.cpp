// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#include "EscapeDirector.h"
#include "CompactAllocation.h"
#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include "CommandWindow.h"
#include "NetworkRetry.h"
#include "SerialJsonLine.h"
#include "StationAddress.h"
#include "StationClock.h"
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

// The description is written from its source whenever it is sent; the JSON-text
// source keeps only the IDs the client checks, the rest stays in flash.
ed::DescriptionSource *description = nullptr;
// Long-lived documents, and the setup request that can become the saved record.
JsonDocument manifest(&compactAllocator), saved(&compactAllocator),
    pendingNetwork(&compactAllocator);
ed::NetworkRetry retry;
ed::CommandWindow<> commands;
String session, game, test, discoveredHost, discoveryNonce, lastStationHost;
std::atomic<uint32_t> leaseUntil{0}, activeGame{0};
uint32_t generation = 0, reportAt = 0, discoveryAt = 0;
// While a USB setup session is active, network work that can block (reconnects,
// state reports) waits, so a setup request larger than the 512-byte UNO R4 serial
// buffer is read as it arrives instead of overflowing behind a modem wait.
uint32_t setupHoldUntil = 0;
bool setupHeld() { return setupHoldUntil && int32_t(millis() - setupHoldUntil) < 0; }
// The station certificate is read only while connecting. Keeping it out of the
// long-lived record saves about 1.3 KB of RAM on a 32 KB board.
void forgetCertificate() { saved["connection"].remove("certificate"); }
// A certificate sent as acknowledged provision-part messages, so no single USB
// request outgrows the 512-byte UNO R4 serial buffer. Bounded; cleared after use.
constexpr size_t MAX_CERTIFICATE_PART = 256, MAX_CERTIFICATE_BYTES = 4096;
String certificateParts;
uint32_t certificatePartCount = 0;
void clearCertificateParts() {
  certificateParts = String();
  certificatePartCount = 0;
}
size_t stationAddressAttempt = 0;
// A failed station connection blocks for the modem's full timeout (10 s on the
// UNO R4, which ignores shorter connect timeouts). Once this controller has
// reached the station, or an attempt has failed, it reconnects only after the
// station answers a discovery query, with a blind attempt at most every two
// minutes in case discovery traffic is filtered.
uint8_t stationFailures = 0;
// Rejoining Wi-Fi runs in the background; start again if it takes too long.
bool joining = false;
uint32_t joinStartedAt = 0;
constexpr uint32_t JOIN_TIMEOUT_MS = 20000;
uint32_t blindAttemptAt = 0;
bool broadcastNext = false, wasLinked = false;
constexpr uint32_t DISCOVERY_INTERVAL_MS = 5000, BLIND_ATTEMPT_MS = 120000;
ed::StationClock stationClock;
bool ready = false, discoveryStarted = false, discardSerialLine = false;
// Bench builds (-DED_BENCH_TRACE) print network steps that block for 50 ms or more.
#ifdef ED_BENCH_TRACE
struct BenchStep {
  const char *name;
  uint32_t start = millis();
  explicit BenchStep(const char *name) : name(name) {}
  ~BenchStep() {
    const uint32_t took = millis() - start;
    if (took >= 50) {
      Serial.print("ED trace ");
      Serial.print(name);
      Serial.print(" ms=");
      Serial.println(took);
    }
  }
};
#define ED_TRACE(name) BenchStep benchStep_##__LINE__(name)
#else
#define ED_TRACE(name)
#endif
// IPAddress stores octets in network order on the bundled cores; compare
// addresses as host-order integers so prefixes line up.
uint32_t hostOrder(uint32_t raw) {
  const uint8_t *b = reinterpret_cast<const uint8_t *>(&raw);
  return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
}
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
// One shared static buffer: each network write is a modem round trip on the
// UNO R4, so larger chunks mean fewer pauses, and the 1 KB main stack stays
// free. Writers are never nested.
uint8_t chunkBuffer[512];
class ChunkedWriter : public Print {
public:
  explicit ChunkedWriter(Print &out) : out(out) {}
  size_t write(uint8_t value) override {
    buffer[used++] = value;
    if (used == sizeof(chunkBuffer))
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
  uint8_t *buffer = chunkBuffer;
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
  return strlen(DESCRIPTION_KEY) + description->descriptionLength() + measureJson(message);
}
void writeDescribed(JsonDocument &message, Print &out) {
  out.print(DESCRIPTION_KEY);
  description->writeDescription(out);
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

// Each report is a TLS write that pauses the loop (about 65 ms on the UNO R4),
// so unchanged state is re-sent only as a heartbeat.
constexpr uint32_t STATE_CHECK_MS = 100, STATE_HEARTBEAT_MS = 2000;
uint32_t stateCheckAt = 0, reportedHash = 0;
class HashPrint : public Print {
public:
  size_t write(uint8_t value) override {
    hash = (hash ^ value) * 16777619UL;
    return 1;
  }
  uint32_t hash = 2166136261UL;
};

void report(bool onlyIfChanged = false) {
  if (!ready)
    return;
  JsonDocument message;
  message["type"] = "custom-state";
  driver->state(message["state"].to<JsonObject>());
  HashPrint fingerprint;
  serializeJson(message["state"], fingerprint);
  if (onlyIfChanged && fingerprint.hash == reportedHash &&
      uint32_t(millis() - reportAt) < STATE_HEARTBEAT_MS)
    return;
  ED_TRACE("report");
  reportedHash = fingerprint.hash;
  publish(message);
  reportAt = millis();
}

void lease(uint32_t remaining) {
  leaseUntil = millis() + remaining;
  if (!test.isEmpty())
    driver->testLease(leaseUntil);
}

bool declared(const char *propId, const char *capability, const char *kind, bool testing = false) {
  return propId && capability &&
         description->declares(propId, capability, strcmp(kind, "commands") == 0, testing);
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
    // Status polls during Verify must not extend the hold: the Connector is then
    // waiting for this controller to reconnect over the network.
    if (strcmp(message["type"] | "", "setup-status") != 0)
      setupHoldUntil = millis() + 30000;
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
      response["firmwareVersion"] = description->firmwareVersion();
      response["firmwareKind"] = "custom";
      // The Connector may send the certificate in acknowledged parts.
      response["provisionParts"] = true;
      clearCertificateParts();
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
    if (type == "provision-part") {
      const char *data = message["data"];
      const size_t length = data ? strlen(data) : 0;
      const uint32_t index = message["index"] | UINT32_MAX;
      if (index == 0)
        clearCertificateParts();
      if (!length || length > MAX_CERTIFICATE_PART || index != certificatePartCount ||
          certificateParts.length() + length > MAX_CERTIFICATE_BYTES ||
          (index == 0 && !certificateParts.reserve(MAX_CERTIFICATE_PART * 5)) ||
          !certificateParts.concat(data)) {
        clearCertificateParts();
        reject("invalid_connection");
        continue;
      }
      ++certificatePartCount;
      response["type"] = "provision-part-accepted";
      response["index"] = index;
      reply();
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
      // Certificate parts serve one request, whatever its outcome. The record
      // that referred to them drops that reference before this branch ends.
      struct ReleaseParts {
        ~ReleaseParts() { clearCertificateParts(); }
      } releaseParts;
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
      // reuses the parsed request; a network change edits the durable record,
      // which is loaded again because the in-memory one omits the certificate.
      const bool provisioning = type == "provision";
      JsonDocument stored;
      JsonDocument &next = provisioning ? message : stored;
      if (provisioning) {
        JsonObject connection = message["connection"];
        // Refer to the assembled parts instead of copying them into the record.
        if (connection["certificate"].isNull() && certificatePartCount)
          connection["certificate"] = JsonString(certificateParts.c_str(), true);
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
      } else if (saved["connection"].isNull() || !board->loadPairing(stored)) {
        reject("invalid_connection");
        continue;
      }
      next["network"] = pendingNetwork;
      if (next.overflowed() || !board->savePairing(next.as<JsonVariantConst>())) {
        reject("storage_failed");
        continue;
      }
      if (provisioning)
        saved = std::move(message);
      else
        saved["network"] = pendingNetwork;
      forgetCertificate();
      pendingNetwork.clear();
      retry.reset();
      mqtt->stop();
      detach();
      setupHoldUntil = 0; // provisioned: let Verify reconnect immediately
      response["type"] = "provisioned";
      response["deviceId"] = saved["connection"]["deviceId"];
      reply();
    } else
      reject("invalid_message");
  }
}

// The saved station address sharing the longest prefix with this controller.
String bestStationAddress() {
  JsonArray addresses = saved["connection"]["addresses"].as<JsonArray>();
  if (!addresses.size())
    return saved["connection"]["host"] | "";
  const uint32_t local = uint32_t(board->localIP());
  const size_t index = ed::stationAddressIndex(0, addresses.size(), [&](size_t candidate) {
    IPAddress address;
    if (!address.fromString(addresses[candidate].as<const char *>()))
      return -1;
    return ed::commonPrefixBits(hostOrder(local), hostOrder(uint32_t(address)));
  });
  return addresses[index].as<String>();
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
  if (uint32_t(millis() - discoveryAt) < DISCOVERY_INTERVAL_MS)
    return;
  discoveryAt = millis();
  discoveryNonce = uuid();
  discoveryNonce.replace("-", "");
  JsonDocument request;
  request["type"] = "find-room-station";
  request["controllerId"] = saved["connection"]["controllerId"];
  request["nonce"] = discoveryNonce;
  // One write per packet: each write is a modem round trip on the UNO R4.
  char packet[160];
  const size_t length = serializeJson(request, packet, sizeof(packet));
  // Alternate a unicast query to the known station, which crosses routed
  // subnets, with a broadcast, which finds a station whose address changed.
  IPAddress target;
  const String known = lastStationHost.isEmpty() ? bestStationAddress() : lastStationHost;
  if (broadcastNext || known.isEmpty() || !target.fromString(known)) {
    IPAddress ip = board->localIP(), mask = board->subnetMask();
    target = IPAddress(ip[0] | ~mask[0], ip[1] | ~mask[1], ip[2] | ~mask[2], ip[3] | ~mask[3]);
  }
  broadcastNext = !broadcastNext;
  board->discoverySocket().beginPacket(target, 43125);
  board->discoverySocket().write(reinterpret_cast<const uint8_t *>(packet), length);
  board->discoverySocket().endPacket();
}
} // namespace
namespace ed {
void begin(DescriptionSource &source, CustomDriver &implementation, BoardAdapter &adapter) {
  board = &adapter;
  board->begin();
  mqtt.reset(new MqttClient(board->secureClient()));
  driver = &implementation;
  description = &source;
  // Non-JSON lines are ignored by setup and visible in a serial monitor.
  const size_t descriptionBytes = description->descriptionLength();
  if (descriptionBytes > 16384) {
    board->setupStream().print(F("Escape Director: description is "));
    board->setupStream().print(descriptionBytes);
    board->setupStream().println(F(" bytes; the limit is 16384."));
  }
  board->loadPairing(saved);
  forgetCertificate();
  mqtt->setConnectionTimeout(1500);
  mqtt->setKeepAliveInterval(20000);
  mqtt->onMessage(receiveMqtt);
}

// Network service costs several modem round trips (about 25 ms on the UNO R4),
// so it runs on a fixed cadence; USB setup and lease expiry run every call.
constexpr uint32_t NETWORK_SERVICE_MS = 50;
uint32_t serviceAt = 0;

void poll() {
  tick();
  receiveSerial();
  if (!pendingNetwork.isNull())
    return;
  if (uint32_t(millis() - serviceAt) < NETWORK_SERVICE_MS)
    return;
  serviceAt = millis();
  // Each connected() is a modem round trip on the UNO R4; ask once per poll.
  const bool linked = mqtt->connected();
  retry.observe(linked, millis());
  if (wasLinked && !linked)
    discoveryAt = millis() - DISCOVERY_INTERVAL_MS; // ask the station at once
  if (wasLinked && !linked)
    blindAttemptAt = millis();
  wasLinked = linked;
#if defined(ED_BENCH_TRACE) && defined(ED_BENCH_DROP_WIFI_MS)
  { // Bench only: drop Wi-Fi once, ED_BENCH_DROP_WIFI_MS after first linking.
    static uint32_t linkedAt = 0;
    static bool dropped = false;
    if (linked && !linkedAt)
      linkedAt = millis();
    if (linkedAt && !dropped && uint32_t(millis() - linkedAt) >= ED_BENCH_DROP_WIFI_MS) {
      dropped = true;
      Serial.println("ED trace dropping wifi");
      board->disconnectNetwork();
    }
  }
#endif
  if (!linked) {
    ED_TRACE("discover");
    discover();
  }
  if (!linked) {
    detach();
    const bool answered = !discoveredHost.isEmpty();
    if (saved["connection"].isNull() || setupHeld() || (!answered && !retry.ready(millis())))
      return;
    // Wi-Fi is up and this is a reconnection: wait for the station to answer.
    if (!answered && (stationFailures || !lastStationHost.isEmpty()) &&
        board->networkConnected()) {
      if (uint32_t(millis() - blindAttemptAt) < BLIND_ATTEMPT_MS)
        return;
      blindAttemptAt = millis();
    }
    retry.attempted(millis());
    struct AttemptEnd {
      ed::NetworkRetry &retry;
      ~AttemptEnd() { retry.finished(millis()); }
    } attemptEnd{retry};
    if (!board->networkConnected()) {
      if (!joining || uint32_t(millis() - joinStartedAt) >= JOIN_TIMEOUT_MS) {
        ED_TRACE("startJoin");
        board->startJoin(saved["network"]["ssid"], saved["network"]["password"]);
        joining = true;
        joinStartedAt = millis();
      }
      return;
    }
    joining = false;
    // Load the durable record only for this attempt; the certificate must stay
    // alive until connect() returns and is released with this document.
    JsonDocument trust;
    if (!board->loadCertificate(trust) || !trust["connection"]["certificate"].is<const char *>())
      return;
    {
      ED_TRACE("trustStation");
      board->trustStation(trust["connection"]["certificate"]);
    }
    mqtt->setId(saved["connection"]["controllerId"].as<const char *>());
    mqtt->setUsernamePassword(saved["connection"]["controllerId"].as<const char *>(), saved["connection"]["password"].as<const char *>());
    JsonArray addresses = saved["connection"]["addresses"].as<JsonArray>();
    String host = saved["connection"]["host"].as<String>();
    if (!discoveredHost.isEmpty()) {
      host = discoveredHost;
      discoveredHost = "";
    } else if (stationAddressAttempt == 0 && !lastStationHost.isEmpty()) {
      // Reconnect to the address that worked last time before trying others.
      host = lastStationHost;
      ++stationAddressAttempt;
    } else if (addresses.size()) {
      const uint32_t local = uint32_t(board->localIP());
      // Two attempts on the most promising address, then the next candidate:
      // unreachable (VPN) addresses stay in the cycle without costing every
      // other attempt a full modem timeout.
      const size_t attempt = stationAddressAttempt++, count = addresses.size();
      const size_t rank = count > 1 && attempt % 3 == 2 ? 1 + (attempt / 3) % (count - 1) : 0;
      const size_t index = ed::stationAddressIndex(
          rank, count, [&](size_t candidate) {
            IPAddress address;
            if (!address.fromString(addresses[candidate].as<const char *>()))
              return -1;
            return ed::commonPrefixBits(hostOrder(local), hostOrder(uint32_t(address)));
          });
      host = addresses[index].as<String>();
    }
    bool connectedNow;
#ifdef ED_BENCH_TRACE
    Serial.print("ED trace host ");
    Serial.print(host);
    Serial.print(" of ");
    serializeJson(addresses, Serial);
    Serial.print(" local=");
    Serial.print(board->localIP());
    Serial.print(" mask=");
    Serial.println(board->subnetMask());
#endif
    {
      ED_TRACE("mqttConnect");
      connectedNow = mqtt->connect(host.c_str(), saved["connection"]["port"].as<int>());
    }
    if (!connectedNow) {
      if (stationFailures < 255)
        ++stationFailures;
      blindAttemptAt = millis();
    }
    if (connectedNow) {
      stationFailures = 0;
      stationAddressAttempt = 0;
      lastStationHost = host;
      String topic = "ed/v1/" + saved["connection"]["controllerId"].as<String>() + "/down";
      mqtt->subscribe(topic);
    }
  }
  {
    ED_TRACE("mqttPoll");
    mqtt->poll();
  }
  tick();
  if (ready && linked && !setupHeld()) {
    if (const uint32_t sequence = stationClock.request(millis())) {
      JsonDocument request;
      request["type"] = "clock-sync";
      request["sequence"] = sequence;
      publish(request);
    }
    if (uint32_t(millis() - stateCheckAt) >= STATE_CHECK_MS) {
      stateCheckAt = millis();
      report(true); // on change, and at least every two seconds
    }
  }
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
