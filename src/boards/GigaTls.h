// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <MbedSSLClient.h>
// Mbed TLS 2.25 checks DNS names, not IP-address SANs. Connect to the
// discovered address while verifying the stable name against this station's CA.
class StationMbedTlsClient : public arduino::MbedSSLClient {
  int configureStation() {
    const int result = setRootCA();
    if (result != NSAPI_ERROR_OK)
      return result;
    static_cast<TLSSocket *>(sock)->set_hostname("room-connector.local");
    return NSAPI_ERROR_OK;
  }

public:
  StationMbedTlsClient() {
    onBeforeConnect(mbed::callback(this, &StationMbedTlsClient::configureStation));
  }
};
class StationTlsClient : public WiFiSSLClient {
protected:
  void newMbedClient() override {
    client.reset(new StationMbedTlsClient());
    client->setNetwork(getNetwork());
  }
};
