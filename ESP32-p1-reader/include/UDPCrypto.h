#pragma once

#include <Arduino.h>

class UDPCrypto
{
public:
    static constexpr uint8_t PROTOCOL_VERSION = 1;

    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t DEVICE_NAME_SIZE = 16;
    static constexpr size_t NONCE_SIZE = 12;
    static constexpr size_t TAG_SIZE = 16;

    static constexpr size_t PLAINTEXT_SIZE =
        sizeof(uint64_t) +   // sequence
        sizeof(uint64_t) +   // timestamp_ms
        sizeof(float);       // net_power

    static constexpr size_t HEADER_SIZE =
        sizeof(uint8_t) +    // version
        DEVICE_NAME_SIZE +
        sizeof(uint64_t) +   // sequence
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
        const uint8_t devicePrivateKey[KEY_SIZE],
        const uint8_t serverPublicKey[KEY_SIZE],
        const char* deviceName
    );

    bool encrypt(
        uint64_t sequence,
        uint64_t timestampMs,
        float netPower,
        uint8_t* output,
        size_t outputSize
    );

    bool ready() const;

private:
    bool generateSessionNonce();
    bool deriveSessionKey();

    static void writeUint64BE(uint8_t* destination, uint64_t value);
    static void writeFloatBE(uint8_t* destination, float value);

    char deviceName_[DEVICE_NAME_SIZE + 1];

    uint8_t devicePrivateKey_[KEY_SIZE];
    uint8_t serverPublicKey_[KEY_SIZE];
    uint8_t sessionKey_[KEY_SIZE];

    uint8_t sessionNonce_[4];

    bool initialized_;
};