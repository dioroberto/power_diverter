#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

#include "UDPCrypto.h"

class UDPServer
{
public:
    UDPServer(
        uint16_t port,
        const uint8_t serverPrivateKey[32],
        const uint8_t devicePublicKey[32],
        const char* deviceName
    );

    bool begin();

    void loop();

    bool ready() const;

    uint32_t packetsReceived() const;
    uint32_t packetsAccepted() const;
    uint32_t packetsRejected() const;

private:
    void processPacket();

    void printPacket(
        const UDPCrypto::Packet& packet,
        const IPAddress& remoteIP,
        uint16_t remotePort
    );

    WiFiUDP udp_;

    uint16_t port_;

    UDPCrypto crypto_;

    bool initialized_;

    uint32_t packetsReceived_;
    uint32_t packetsAccepted_;
    uint32_t packetsRejected_;
};