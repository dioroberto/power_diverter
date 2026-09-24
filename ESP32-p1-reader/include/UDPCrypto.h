#pragma once

#include <Arduino.h>

#include "UDPProtocol.h"


class UDPCrypto
{
public:

    // ========================================================
    // Protocol
    // ========================================================

    static constexpr uint8_t PROTOCOL_VERSION =
        UDPProtocol::VERSION;


    // ========================================================
    // Cryptographic sizes
    // ========================================================

    static constexpr size_t KEY_SIZE = 32;

    static constexpr size_t DEVICE_NAME_SIZE = 16;

    static constexpr size_t NONCE_SIZE = 12;

    static constexpr size_t TAG_SIZE = 16;


    // ========================================================
    // Plaintext
    //
    // The encrypted plaintext consists of:
    //
    //   sequence
    //   timestamp
    //   application payload
    //
    // The current application payload is P1Metrics.
    // ========================================================

    static constexpr size_t PLAINTEXT_METADATA_SIZE =
        UDPProtocol::ENCRYPTED_METADATA_SIZE;


    static constexpr size_t PLAINTEXT_SIZE =
        UDPProtocol::ENCRYPTED_PAYLOAD_SIZE;


    // ========================================================
    // Packet header
    //
    //   version
    //   device name
    //   sequence
    //   nonce
    // ========================================================

    static constexpr size_t HEADER_SIZE =
        sizeof(uint8_t) +
        DEVICE_NAME_SIZE +
        sizeof(uint64_t) +
        NONCE_SIZE;


    // ========================================================
    // Packet size
    // ========================================================

    static constexpr size_t PACKET_SIZE =
        HEADER_SIZE +
        PLAINTEXT_SIZE +
        TAG_SIZE;


    // ========================================================
    // Constructor
    // ========================================================

    UDPCrypto();


    // ========================================================
    // Initialization
    // ========================================================

    bool begin(
        const uint8_t devicePrivateKey[KEY_SIZE],
        const uint8_t serverPublicKey[KEY_SIZE],
        const char* deviceName
    );


    // ========================================================
    // Encrypt
    // ========================================================

    bool encrypt(
        uint64_t sequence,
        uint64_t timestampMs,
        const uint8_t* payload,
        size_t payloadSize,
        uint8_t* output,
        size_t outputSize
    );


    // ========================================================
    // Status
    // ========================================================

    bool ready() const;


private:

    // ========================================================
    // Session setup
    // ========================================================

    bool generateSessionNonce();

    bool deriveSessionKey();


    // ========================================================
    // Serialization
    // ========================================================

    static void writeUint64BE(
        uint8_t* destination,
        uint64_t value
    );


    // ========================================================
    // Device identity
    // ========================================================

    char deviceName_[DEVICE_NAME_SIZE + 1];


    // ========================================================
    // Cryptographic material
    // ========================================================

    uint8_t devicePrivateKey_[KEY_SIZE];

    uint8_t serverPublicKey_[KEY_SIZE];

    uint8_t sessionKey_[KEY_SIZE];


    // ========================================================
    // Session nonce
    // ========================================================

    uint8_t sessionNonce_[4];


    // ========================================================
    // State
    // ========================================================

    bool initialized_;
};