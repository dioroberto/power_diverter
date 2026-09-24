#include "UDPCrypto.h"
#include "udp_config.h"

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
    // Secure memory clearing
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
    if (devicePrivateKey == nullptr ||
        serverPublicKey == nullptr ||
        deviceName == nullptr)
    {
        initialized_ = false;
        return false;
    }


    const size_t nameLength =
        strlen(deviceName);


    if (nameLength == 0 ||
        nameLength >= sizeof(deviceName_))
    {
        initialized_ = false;
        return false;
    }


    // --------------------------------------------------------
    // Clear previous state
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
    // Store credentials
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
    // Store device name
    // --------------------------------------------------------

    memcpy(
        deviceName_,
        deviceName,
        nameLength);

    deviceName_[nameLength] = '\0';


    // --------------------------------------------------------
    // Initialize RNG
    // --------------------------------------------------------

    RNG.begin(UDPConfig::HKDF_INFO);


    // --------------------------------------------------------
    // Generate session nonce
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
            UDPConfig::HKDF_INFO
        ),
        strlen(UDPConfig::HKDF_INFO)
    );


    // --------------------------------------------------------
    // Erase shared secret
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
// Encrypt UDP packet
// ============================================================

bool UDPCrypto::encrypt(
    uint64_t sequence,
    uint64_t timestampMs,
    const uint8_t* payload,
    size_t payloadSize,
    uint8_t* output,
    size_t outputSize)
{
    // --------------------------------------------------------
    // Validate arguments
    // --------------------------------------------------------

    if (!initialized_ ||
        payload == nullptr ||
        output == nullptr ||
        payloadSize != UDPProtocol::P1_METRICS_SIZE)
    {
        return false;
    }


    if (outputSize < PACKET_SIZE)
    {
        return false;
    }


    // --------------------------------------------------------
    // Clear output
    // --------------------------------------------------------

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
    // 4-byte session nonce
    // +
    // 8-byte sequence
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
    // Verify header size
    // --------------------------------------------------------

    if (offset != HEADER_SIZE)
    {
        return false;
    }


    // ========================================================
    // Encrypted plaintext
    //
    //   sequence
    //   timestamp
    //   P1Metrics
    // ========================================================

    uint8_t plaintext[
        PLAINTEXT_SIZE
    ] = {};


    size_t plainOffset = 0;


    // --------------------------------------------------------
    // Sequence
    // --------------------------------------------------------

    writeUint64BE(
        plaintext + plainOffset,
        sequence);

    plainOffset += sizeof(uint64_t);


    // --------------------------------------------------------
    // Timestamp
    // --------------------------------------------------------

    writeUint64BE(
        plaintext + plainOffset,
        timestampMs);

    plainOffset += sizeof(uint64_t);


    // --------------------------------------------------------
    // P1 metrics
    // --------------------------------------------------------

    memcpy(
        plaintext + plainOffset,
        payload,
        payloadSize);


    // ========================================================
    // ChaCha20-Poly1305
    // ========================================================

    const uint8_t* nonce =
        output +
        sizeof(uint8_t) +
        DEVICE_NAME_SIZE +
        sizeof(uint64_t);


    uint8_t* ciphertext =
        output + HEADER_SIZE;


    uint8_t* tag =
        ciphertext + PLAINTEXT_SIZE;


    ChaChaPoly cipher;


    // --------------------------------------------------------
    // Encryption key
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
    // Packet nonce
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
    // Authenticate header
    // --------------------------------------------------------

    cipher.addAuthData(
        output,
        HEADER_SIZE);


    // --------------------------------------------------------
    // Encrypt
    // --------------------------------------------------------

    cipher.encrypt(
        ciphertext,
        plaintext,
        PLAINTEXT_SIZE);


    // --------------------------------------------------------
    // Authentication tag
    // --------------------------------------------------------

    cipher.computeTag(
        tag,
        TAG_SIZE);


    // --------------------------------------------------------
    // Erase plaintext
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