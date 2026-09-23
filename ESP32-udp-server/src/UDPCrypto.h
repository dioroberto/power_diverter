#pragma once

#include <Arduino.h>

class UDPCrypto
{
public:
    static constexpr uint8_t PROTOCOL_VERSION = 1;

    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;
    static constexpr size_t TAG_SIZE = 16;

    static constexpr size_t DEVICE_NAME_SIZE = 16;

    static constexpr size_t PLAINTEXT_SIZE =
        sizeof(uint64_t) +
        sizeof(uint64_t) +
        sizeof(float);

    static constexpr size_t HEADER_SIZE =
        sizeof(uint8_t) +
        DEVICE_NAME_SIZE +
        sizeof(uint64_t) +
        NONCE_SIZE;

    static constexpr size_t PACKET_SIZE =
        HEADER_SIZE +
        PLAINTEXT_SIZE +
        TAG_SIZE;

    struct Packet
    {
        uint64_t sequence;
        uint64_t timestampMs;
        float netPower;
    };

    UDPCrypto();

    bool begin(
        const uint8_t serverPrivateKey[KEY_SIZE],
        const uint8_t devicePublicKey[KEY_SIZE],
        const char* deviceName
    );

    bool decrypt(
        const uint8_t* packet,
        size_t packetSize,
        Packet& result
    );

    bool ready() const;

    const char* deviceName() const;

private:
    bool deriveSessionKey();

    static void writeUint64BE(
        uint8_t* destination,
        uint64_t value
    );

    static uint64_t readUint64BE(
        const uint8_t* source
    );

    static float readFloatBE(
        const uint8_t* source
    );

    char deviceName_[DEVICE_NAME_SIZE + 1];

    uint8_t serverPrivateKey_[KEY_SIZE];
    uint8_t devicePublicKey_[KEY_SIZE];
    uint8_t sessionKey_[KEY_SIZE];

    bool initialized_;
};