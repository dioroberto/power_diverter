#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

#include "P1Reader.h"
#include "UDPCrypto.h"


class UDPClient
{
public:

    UDPClient(
        const char* hostname,
        uint16_t port
    );

    bool begin(
        bool mdnsAvailable
    );

    void loop();

    void sendNetPower(
        const P1Reader& p1Reader
    );

    bool ready() const;

    IPAddress serverIP() const;


private:

    bool resolveServer();

    void markUnavailable();


    // ========================================================
    // Configuration
    // ========================================================

    const char* hostname_;
    uint16_t port_;


    // ========================================================
    // UDP
    // ========================================================

    WiFiUDP udp_;


    // ========================================================
    // Cryptography
    // ========================================================

    UDPCrypto crypto_;


    // ========================================================
    // Server state
    // ========================================================

    IPAddress serverIP_;

    bool mdnsAvailable_;
    bool ready_;


    // ========================================================
    // Timers
    // ========================================================

    uint32_t lastResolve_;

    static constexpr uint32_t RESOLVE_INTERVAL_MS =
        10UL * 1000UL;

    static constexpr uint32_t RERESOLVE_INTERVAL_MS =
        5UL * 60UL * 1000UL;
};