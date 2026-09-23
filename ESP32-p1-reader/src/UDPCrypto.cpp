#include "UDPCrypto.h"

#include <Crypto.h>
#include <Curve25519.h>
#include <ChaChaPoly.h>
#include <HKDF.h>
#include <SHA256.h>
#include <RNG.h>

#include <cstring>

namespace
{
    // ========================================================
    // Protocol constants
    // ========================================================

    constexpr char HKDF_INFO[] =
        "DIONISIOTECH-P1-UDP-v1";

    constexpr size_t HKDF_INFO_SIZE =
        sizeof(HKDF_INFO) - 1;

    // ========================================================
    // Secure memory clearing
    //
    // Do not use Crypto::clean() here because Crypto.h may
    // provide overloaded clean() functions that can cause
    // ambiguity with array types.
    // ========================================================

    void secureClean(
        void* ptr,
        size_t size)
    {
        volatile uint8_t* p =
            static_cast<volatile uint8_t*>(ptr);

        while (size-- > 0)
        {
            *p++ = 0;
        }
    }
}

// ============================================================
// Constructor
// ============================================================

UDPCrypto::UDPCrypto()
    : deviceName_{},
      devicePrivateKey_{},
      serverPublicKey_{},
      sessionKey_{},
      sessionNonce_{},
      initialized_(false)
{
}

// ============================================================
// Begin
// ============================================================

bool UDPCrypto::begin(
    const uint8_t devicePrivateKey[KEY_SIZE],
    const uint8_t serverPublicKey[KEY_SIZE],
    const char* deviceName)
{
    // --------------------------------------------------------
    // Validate arguments
    // --------------------------------------------------------

    if (devicePrivateKey == nullptr ||
        serverPublicKey == nullptr ||
        deviceName == nullptr)
    {
        initialized_ = false;
        return false;
    }

    // --------------------------------------------------------
    // Validate device name
    // --------------------------------------------------------

    const size_t nameLength =
        strlen(deviceName);

    if (nameLength == 0 ||
        nameLength >= sizeof(deviceName_))
    {
        initialized_ = false;
        return false;
    }

    // --------------------------------------------------------
    // Reset previous state.
    //
    // This makes repeated begin() calls safe.
    // --------------------------------------------------------

    secureClean(
        devicePrivateKey_,
        sizeof(devicePrivateKey_));

    secureClean(
        serverPublicKey_,
        sizeof(serverPublicKey_));

    secureClean(
        sessionKey_,
        sizeof(sessionKey_));

    secureClean(
        sessionNonce_,
        sizeof(sessionNonce_));

    secureClean(
        deviceName_,
        sizeof(deviceName_));

    initialized_ = false;

    // --------------------------------------------------------
    // Copy device credentials
    // --------------------------------------------------------

    memcpy(
        devicePrivateKey_,
        devicePrivateKey,
        KEY_SIZE);

    memcpy(
        serverPublicKey_,
        serverPublicKey,
        KEY_SIZE);

    // --------------------------------------------------------
    // Copy device name
    // --------------------------------------------------------

    memcpy(
        deviceName_,
        deviceName,
        nameLength);

    deviceName_[nameLength] = '\0';

    // --------------------------------------------------------
    // Initialize RNG
    // --------------------------------------------------------

    RNG.begin(HKDF_INFO);

    // --------------------------------------------------------
    // Generate fresh session nonce
    // --------------------------------------------------------

    if (!generateSessionNonce())
    {
        secureClean(
            devicePrivateKey_,
            sizeof(devicePrivateKey_));

        secureClean(
            serverPublicKey_,
            sizeof(serverPublicKey_));

        secureClean(
            sessionKey_,
            sizeof(sessionKey_));

        secureClean(
            sessionNonce_,
            sizeof(sessionNonce_));

        secureClean(
            deviceName_,
            sizeof(deviceName_));

        return false;
    }

    // --------------------------------------------------------
    // Derive session key
    // --------------------------------------------------------

    if (!deriveSessionKey())
    {
        secureClean(
            devicePrivateKey_,
            sizeof(devicePrivateKey_));

        secureClean(
            serverPublicKey_,
            sizeof(serverPublicKey_));

        secureClean(
            sessionKey_,
            sizeof(sessionKey_));

        secureClean(
            sessionNonce_,
            sizeof(sessionNonce_));

        secureClean(
            deviceName_,
            sizeof(deviceName_));

        return false;
    }

    initialized_ = true;

    return true;
}

// ============================================================
// Generate session nonce
// ============================================================

bool UDPCrypto::generateSessionNonce()
{
    constexpr size_t nonceSize =
        sizeof(sessionNonce_);

    for (uint32_t attempt = 0;
         attempt < 100;
         ++attempt)
    {
        if (RNG.available(nonceSize))
        {
            RNG.rand(
                sessionNonce_,
                nonceSize);

            return true;
        }

        RNG.loop();
        delay(1);
    }

    return false;
}

// ============================================================
// Derive session key
// ============================================================

bool UDPCrypto::deriveSessionKey()
{
    uint8_t sharedSecret[KEY_SIZE] = {};

    // --------------------------------------------------------
    // X25519
    // --------------------------------------------------------

    if (!Curve25519::eval(
            sharedSecret,
            devicePrivateKey_,
            serverPublicKey_))
    {
        secureClean(
            sharedSecret,
            sizeof(sharedSecret));

        return false;
    }

    // --------------------------------------------------------
    // HKDF-SHA256
    // --------------------------------------------------------

    HKDF<SHA256> hkdf;

    hkdf.setKey(
        sharedSecret,
        sizeof(sharedSecret));

    hkdf.extract(
        sessionKey_,
        sizeof(sessionKey_),
        reinterpret_cast<const uint8_t*>(
            HKDF_INFO),
        HKDF_INFO_SIZE);

    // --------------------------------------------------------
    // Never retain the X25519 shared secret.
    // --------------------------------------------------------

    secureClean(
        sharedSecret,
        sizeof(sharedSecret));

    return true;
}

// ============================================================
// Write uint64_t big-endian
// ============================================================

void UDPCrypto::writeUint64BE(
    uint8_t* destination,
    uint64_t value)
{
    for (int i = 7; i >= 0; --i)
    {
        destination[i] =
            static_cast<uint8_t>(
                value & 0xFFU);

        value >>= 8;
    }
}

// ============================================================
// Write float big-endian
// ============================================================

void UDPCrypto::writeFloatBE(
    uint8_t* destination,
    float value)
{
    static_assert(
        sizeof(float) == sizeof(uint32_t),
        "This protocol requires 32-bit float");

    uint32_t raw = 0;

    memcpy(
        &raw,
        &value,
        sizeof(raw));

    destination[0] =
        static_cast<uint8_t>(
            (raw >> 24) & 0xFFU);

    destination[1] =
        static_cast<uint8_t>(
            (raw >> 16) & 0xFFU);

    destination[2] =
        static_cast<uint8_t>(
            (raw >> 8) & 0xFFU);

    destination[3] =
        static_cast<uint8_t>(
            raw & 0xFFU);
}

// ============================================================
// Encrypt UDP packet
// ============================================================

bool UDPCrypto::encrypt(
    uint64_t sequence,
    uint64_t timestampMs,
    float netPower,
    uint8_t* output,
    size_t outputSize)
{
    // --------------------------------------------------------
    // Validate state and output buffer
    // --------------------------------------------------------

    if (!initialized_ ||
        output == nullptr ||
        outputSize < PACKET_SIZE)
    {
        return false;
    }

    memset(
        output,
        0,
        PACKET_SIZE);

    // ========================================================
    // Header
    // ========================================================

    size_t offset = 0;

    // --------------------------------------------------------
    // Protocol version
    // --------------------------------------------------------

    output[offset++] =
        PROTOCOL_VERSION;

    // --------------------------------------------------------
    // Device name
    //
    // Fixed-size, zero-padded field.
    // --------------------------------------------------------

    memcpy(
        output + offset,
        deviceName_,
        DEVICE_NAME_SIZE);

    offset += DEVICE_NAME_SIZE;

    // --------------------------------------------------------
    // Sequence
    // --------------------------------------------------------

    writeUint64BE(
        output + offset,
        sequence);

    offset += sizeof(uint64_t);

    // --------------------------------------------------------
    // Nonce
    //
    // 4-byte random session nonce
    // +
    // 8-byte packet sequence
    // --------------------------------------------------------

    memcpy(
        output + offset,
        sessionNonce_,
        sizeof(sessionNonce_));

    offset += sizeof(sessionNonce_);

    writeUint64BE(
        output + offset,
        sequence);

    offset += sizeof(uint64_t);

    // --------------------------------------------------------
    // Verify calculated header size.
    // --------------------------------------------------------

    if (offset != HEADER_SIZE)
    {
        return false;
    }

    // ========================================================
    // Plaintext
    // ========================================================

    uint8_t plaintext[PLAINTEXT_SIZE] = {};

    size_t plainOffset = 0;

    // Sequence
    writeUint64BE(
        plaintext + plainOffset,
        sequence);

    plainOffset += sizeof(uint64_t);

    // Timestamp
    writeUint64BE(
        plaintext + plainOffset,
        timestampMs);

    plainOffset += sizeof(uint64_t);

    // Net power
    writeFloatBE(
        plaintext + plainOffset,
        netPower);

    // ========================================================
    // ChaCha20-Poly1305
    // ========================================================

    // The nonce starts after:
    //
    //   1 byte  protocol version
    //   N bytes device name
    //   8 bytes sequence
    //
    const uint8_t* nonce =
        output +
        1 +
        DEVICE_NAME_SIZE +
        sizeof(uint64_t);

    uint8_t* ciphertext =
        output + HEADER_SIZE;

    uint8_t* tag =
        ciphertext + PLAINTEXT_SIZE;

    ChaChaPoly cipher;

    // --------------------------------------------------------
    // Set encryption key
    // --------------------------------------------------------

    if (!cipher.setKey(
            sessionKey_,
            sizeof(sessionKey_)))
    {
        secureClean(
            plaintext,
            sizeof(plaintext));

        return false;
    }

    // --------------------------------------------------------
    // Set packet nonce
    // --------------------------------------------------------

    if (!cipher.setIV(
            nonce,
            NONCE_SIZE))
    {
        secureClean(
            plaintext,
            sizeof(plaintext));

        return false;
    }

    // --------------------------------------------------------
    // Authenticate the complete header.
    //
    // The header is transmitted in cleartext but protected
    // by the Poly1305 authentication tag.
    // --------------------------------------------------------

    cipher.addAuthData(
        output,
        HEADER_SIZE);

    // --------------------------------------------------------
    // Encrypt plaintext
    // --------------------------------------------------------

    cipher.encrypt(
        ciphertext,
        plaintext,
        PLAINTEXT_SIZE);

    // --------------------------------------------------------
    // Generate authentication tag
    // --------------------------------------------------------

    cipher.computeTag(
        tag,
        TAG_SIZE);

    // --------------------------------------------------------
    // Erase plaintext immediately.
    // --------------------------------------------------------

    secureClean(
        plaintext,
        sizeof(plaintext));

    return true;
}

// ============================================================
// Ready
// ============================================================

bool UDPCrypto::ready() const
{
    return initialized_;
}