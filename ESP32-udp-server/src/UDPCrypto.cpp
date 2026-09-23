#include "UDPCrypto.h"

#include <Crypto.h>
#include <Curve25519.h>
#include <ChaChaPoly.h>
#include <HKDF.h>
#include <SHA256.h>

#include <cstring>

namespace
{
    constexpr char HKDF_INFO[] =
        "DIONISIOTECH-P1-UDP-v1";

    constexpr size_t HKDF_INFO_SIZE =
        sizeof(HKDF_INFO) - 1;
}


// ============================================================
// Constructor
// ============================================================

UDPCrypto::UDPCrypto()
    : deviceName_{},
      serverPrivateKey_{},
      devicePublicKey_{},
      sessionKey_{},
      initialized_(false)
{
}


// ============================================================
// Begin
// ============================================================

bool UDPCrypto::begin(
    const uint8_t serverPrivateKey[KEY_SIZE],
    const uint8_t devicePublicKey[KEY_SIZE],
    const char* deviceName)
{
    if (serverPrivateKey == nullptr ||
        devicePublicKey == nullptr ||
        deviceName == nullptr)
    {
        return false;
    }

    if (deviceName[0] == '\0')
        return false;

    const size_t nameLength =
        strlen(deviceName);

    if (nameLength > DEVICE_NAME_SIZE)
    {
        return false;
    }

    memset(
        deviceName_,
        0,
        sizeof(deviceName_)
    );

    memcpy(
        deviceName_,
        deviceName,
        nameLength
    );

    memcpy(
        serverPrivateKey_,
        serverPrivateKey,
        KEY_SIZE
    );

    memcpy(
        devicePublicKey_,
        devicePublicKey,
        KEY_SIZE
    );

    if (!deriveSessionKey())
    {
        initialized_ = false;
        return false;
    }

    initialized_ = true;

    return true;
}


// ============================================================
// Derive session key
// ============================================================

bool UDPCrypto::deriveSessionKey()
{
    uint8_t sharedSecret[KEY_SIZE];

    memset(
        sharedSecret,
        0,
        sizeof(sharedSecret)
    );

    /*
     * X25519:
     *
     * server private × device public
     *
     * This must produce exactly the same shared secret as:
     *
     * device private × server public
     */
    if (!Curve25519::eval(
            sharedSecret,
            serverPrivateKey_,
            devicePublicKey_))
    {
        clean(
            sharedSecret,
            sizeof(sharedSecret)
        );

        return false;
    }

    HKDF<SHA256> hkdf;

    hkdf.setKey(
        sharedSecret,
        sizeof(sharedSecret)
    );

    /*
     * Match the sender exactly.
     */
    hkdf.extract(
        sessionKey_,
        sizeof(sessionKey_),
        reinterpret_cast<const uint8_t*>(
            HKDF_INFO
        ),
        HKDF_INFO_SIZE
    );

    clean(
        sharedSecret,
        sizeof(sharedSecret)
    );

    return true;
}


// ============================================================
// Decrypt packet
// ============================================================

bool UDPCrypto::decrypt(
    const uint8_t* packet,
    size_t packetSize,
    Packet& result)
{
    if (!initialized_)
        return false;

    if (packet == nullptr)
        return false;

    if (packetSize != PACKET_SIZE)
        return false;


    // --------------------------------------------------------
    // Validate protocol version
    // --------------------------------------------------------

    if (packet[0] != PROTOCOL_VERSION)
        return false;


    // --------------------------------------------------------
    // Validate device name
    // --------------------------------------------------------

    if (memcmp(
            packet + 1,
            deviceName_,
            DEVICE_NAME_SIZE) != 0)
    {
        return false;
    }


    // --------------------------------------------------------
    // Extract nonce
    // --------------------------------------------------------

    const uint8_t* nonce =
        packet +
        1 +
        DEVICE_NAME_SIZE +
        sizeof(uint64_t);


    // --------------------------------------------------------
    // Extract ciphertext
    // --------------------------------------------------------

    const uint8_t* ciphertext =
        packet + HEADER_SIZE;


    // --------------------------------------------------------
    // Extract authentication tag
    // --------------------------------------------------------

    const uint8_t* tag =
        ciphertext +
        PLAINTEXT_SIZE;


    // --------------------------------------------------------
    // Decrypt
    // --------------------------------------------------------

    uint8_t plaintext[PLAINTEXT_SIZE];

    ChaChaPoly cipher;

    if (!cipher.setKey(
            sessionKey_,
            sizeof(sessionKey_)))
    {
        return false;
    }

    if (!cipher.setIV(
            nonce,
            NONCE_SIZE))
    {
        return false;
    }


    // --------------------------------------------------------
    // Authenticate header
    // --------------------------------------------------------

    cipher.addAuthData(
        packet,
        HEADER_SIZE
    );


    // --------------------------------------------------------
    // Decrypt ciphertext
    // --------------------------------------------------------

    cipher.decrypt(
        plaintext,
        ciphertext,
        PLAINTEXT_SIZE
    );


    // --------------------------------------------------------
    // Verify authentication tag
    // --------------------------------------------------------

    if (!cipher.checkTag(
            tag,
            TAG_SIZE))
    {
        clean(
            plaintext,
            sizeof(plaintext)
        );

        return false;
    }


    // --------------------------------------------------------
    // Decode plaintext
    // --------------------------------------------------------

    result.sequence =
        readUint64BE(
            plaintext
        );

    result.timestampMs =
        readUint64BE(
            plaintext + sizeof(uint64_t)
        );

    result.netPower =
        readFloatBE(
            plaintext +
            sizeof(uint64_t) * 2
        );


    // --------------------------------------------------------
    // Clear sensitive temporary data
    // --------------------------------------------------------

    clean(
        plaintext,
        sizeof(plaintext)
    );

    return true;
}


// ============================================================
// Read uint64 big endian
// ============================================================

uint64_t UDPCrypto::readUint64BE(
    const uint8_t* source)
{
    return
        (static_cast<uint64_t>(source[0]) << 56) |
        (static_cast<uint64_t>(source[1]) << 48) |
        (static_cast<uint64_t>(source[2]) << 40) |
        (static_cast<uint64_t>(source[3]) << 32) |
        (static_cast<uint64_t>(source[4]) << 24) |
        (static_cast<uint64_t>(source[5]) << 16) |
        (static_cast<uint64_t>(source[6]) << 8)  |
        (static_cast<uint64_t>(source[7]));
}


// ============================================================
// Read float big endian
// ============================================================

float UDPCrypto::readFloatBE(
    const uint8_t* source)
{
    static_assert(
        sizeof(float) == 4,
        "UDP protocol requires 32-bit float"
    );

    uint32_t raw =
        (static_cast<uint32_t>(source[0]) << 24) |
        (static_cast<uint32_t>(source[1]) << 16) |
        (static_cast<uint32_t>(source[2]) << 8)  |
        static_cast<uint32_t>(source[3]);

    float value;

    memcpy(
        &value,
        &raw,
        sizeof(value)
    );

    return value;
}


// ============================================================
// Ready
// ============================================================

bool UDPCrypto::ready() const
{
    return initialized_;
}


// ============================================================
// Device name
// ============================================================

const char* UDPCrypto::deviceName() const
{
    return deviceName_;
}