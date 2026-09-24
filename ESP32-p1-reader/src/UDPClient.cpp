#include "UDPClient.h"

#include <ESPmDNS.h>

#include "UDPProtocol.h"
#include "secrets_udp.h"
#include "udp_config.h"


// =============================================================================
// Constructor
// =============================================================================

UDPClient::UDPClient(
    const char* hostname,
    uint16_t port
)
    : hostname_(hostname),
      port_(port),
      serverIP_(INADDR_NONE),
      mdnsAvailable_(false),
      ready_(false),
      lastResolve_(0),
      sequence_(0)
{
}


// =============================================================================
// Begin
// =============================================================================

bool UDPClient::begin(
    bool mdnsAvailable
)
{
    // -------------------------------------------------------------------------
    // Reset runtime state
    // -------------------------------------------------------------------------

    mdnsAvailable_ = mdnsAvailable;
    ready_ = false;
    serverIP_ = INADDR_NONE;

    // Allow an immediate resolution attempt.
    lastResolve_ = 0;


    // -------------------------------------------------------------------------
    // Start UDP socket
    // -------------------------------------------------------------------------

    if (!udp_.begin(0))
    {
        Serial.println(
            "[UDP] ERROR: Failed to start socket."
        );

        return false;
    }


    // -------------------------------------------------------------------------
    // Initialize cryptography
    // -------------------------------------------------------------------------

    if (!crypto_.begin(
            SecretsUDP::UDP::DEVICE_PRIVATE_KEY,
            SecretsUDP::UDP::SERVER_PUBLIC_KEY,
            UDPConfig::DEVICE_NAME
        ))
    {
        Serial.println(
            "[UDP] ERROR: Crypto initialization failed."
        );

        return false;
    }


    Serial.println(
        "[UDP] Client started."
    );

    Serial.println(
        "[UDP] Encryption enabled."
    );


    // -------------------------------------------------------------------------
    // Initial server resolution
    // -------------------------------------------------------------------------

    if (mdnsAvailable_)
    {
        resolveServer();
    }
    else
    {
        Serial.println(
            "[UDP] mDNS unavailable; "
            "destination will be resolved when mDNS becomes available."
        );
    }


    return true;
}


// =============================================================================
// mDNS state
// =============================================================================

void UDPClient::setMDNSAvailable(
    bool available
)
{
    // -------------------------------------------------------------------------
    // No state change
    // -------------------------------------------------------------------------

    if (mdnsAvailable_ == available)
        return;


    mdnsAvailable_ = available;


    // -------------------------------------------------------------------------
    // mDNS lost
    // -------------------------------------------------------------------------

    if (!available)
    {
        Serial.println(
            "[UDP] mDNS unavailable."
        );

        markUnavailable();

        return;
    }


    // -------------------------------------------------------------------------
    // mDNS restored
    // -------------------------------------------------------------------------

    Serial.println(
        "[UDP] mDNS available."
    );


    // Force an immediate resolution attempt.
    lastResolve_ = 0;
}


// =============================================================================
// Loop
// =============================================================================

void UDPClient::loop()
{
    if (!mdnsAvailable_)
        return;


    const uint32_t now =
        millis();


    const uint32_t interval =
        ready_
            ? RERESOLVE_INTERVAL_MS
            : RESOLVE_INTERVAL_MS;


    if (
        static_cast<uint32_t>(
            now - lastResolve_
        ) < interval
    )
    {
        return;
    }


    lastResolve_ = now;

    resolveServer();
}


// =============================================================================
// Resolve server
// =============================================================================

bool UDPClient::resolveServer()
{
    if (!mdnsAvailable_)
        return false;


    Serial.printf(
        "[UDP] Resolving %s...\n",
        hostname_
    );


    const IPAddress resolved =
        MDNS.queryHost(hostname_);


    // -------------------------------------------------------------------------
    // Resolution failed
    // -------------------------------------------------------------------------

    if (resolved == INADDR_NONE)
    {
        markUnavailable();

        Serial.printf(
            "[UDP] Server %s unavailable.\n",
            hostname_
        );

        return false;
    }


    // -------------------------------------------------------------------------
    // Check whether the address changed
    // -------------------------------------------------------------------------

    const bool addressChanged =
        !ready_ ||
        serverIP_ != resolved;


    // -------------------------------------------------------------------------
    // Store destination
    // -------------------------------------------------------------------------

    serverIP_ = resolved;
    ready_ = true;


    // -------------------------------------------------------------------------
    // Report new destination
    // -------------------------------------------------------------------------

    if (addressChanged)
    {
        const String address =
            serverIP_.toString();

        Serial.printf(
            "[UDP] Server resolved: %s\n",
            address.c_str()
        );

        Serial.printf(
            "[UDP] Destination: %s:%u\n",
            address.c_str(),
            static_cast<unsigned int>(port_)
        );
    }


    return true;
}


// =============================================================================
// Mark destination unavailable
// =============================================================================

void UDPClient::markUnavailable()
{
    ready_ = false;
    serverIP_ = INADDR_NONE;
}


// =============================================================================
// Send P1 metrics
// =============================================================================

void UDPClient::sendMetrics(
    const P1Reader& p1Reader
)
{
    // -------------------------------------------------------------------------
    // Destination unavailable
    // -------------------------------------------------------------------------

    if (!ready_)
    {
        Serial.println(
            "[UDP] No destination; packet skipped."
        );

        return;
    }


    // -------------------------------------------------------------------------
    // Build P1 metrics
    // -------------------------------------------------------------------------

    UDPProtocol::P1Metrics metrics{};

    metrics.importedEnergyTariff1 =
        p1Reader.importedEnergyTariff1();

    metrics.importedEnergyTariff2 =
        p1Reader.importedEnergyTariff2();

    metrics.exportedEnergyTariff1 =
        p1Reader.exportedEnergyTariff1();

    metrics.exportedEnergyTariff2 =
        p1Reader.exportedEnergyTariff2();


    metrics.importedPower =
        p1Reader.importedPower();

    metrics.exportedPower =
        p1Reader.exportedPower();

    metrics.netPower =
        p1Reader.netPower();


    metrics.voltagePhase1 =
        p1Reader.voltagePhase1();

    metrics.voltagePhase2 =
        p1Reader.voltagePhase2();

    metrics.voltagePhase3 =
        p1Reader.voltagePhase3();


    metrics.currentPhase1 =
        p1Reader.currentPhase1();

    metrics.currentPhase2 =
        p1Reader.currentPhase2();

    metrics.currentPhase3 =
        p1Reader.currentPhase3();


    // -------------------------------------------------------------------------
    // Sequence
    // -------------------------------------------------------------------------

    ++sequence_;


    // -------------------------------------------------------------------------
    // Monotonic device timestamp
    // -------------------------------------------------------------------------

    const uint64_t timestamp =
        static_cast<uint64_t>(millis());


    // -------------------------------------------------------------------------
    // Encrypted packet
    // -------------------------------------------------------------------------

    uint8_t packet[
        UDPCrypto::PACKET_SIZE
    ];


    if (!crypto_.encrypt(
            sequence_,
            timestamp,
            reinterpret_cast<const uint8_t*>(&metrics),
            sizeof(metrics),
            packet,
            sizeof(packet)
        ))
    {
        Serial.println(
            "[UDP] ERROR: Encryption failed."
        );

        return;
    }


    // -------------------------------------------------------------------------
    // Start UDP packet
    // -------------------------------------------------------------------------

    if (!udp_.beginPacket(
            serverIP_,
            port_
        ))
    {
        Serial.println(
            "[UDP] ERROR: beginPacket() failed."
        );

        return;
    }


    // -------------------------------------------------------------------------
    // Write encrypted packet
    // -------------------------------------------------------------------------

    const size_t written =
        udp_.write(
            packet,
            sizeof(packet)
        );


    if (written != sizeof(packet))
    {
        Serial.printf(
            "[UDP] ERROR: write failed: %u/%u bytes\n",
            static_cast<unsigned int>(written),
            static_cast<unsigned int>(sizeof(packet))
        );

        udp_.endPacket();

        return;
    }


    // -------------------------------------------------------------------------
    // Send packet
    // -------------------------------------------------------------------------

    if (!udp_.endPacket())
    {
        Serial.println(
            "[UDP] ERROR: endPacket() failed."
        );

        return;
    }


    // -------------------------------------------------------------------------
    // Diagnostic
    // -------------------------------------------------------------------------

    Serial.printf(
        "[UDP] Encrypted P1 packet sent: "
        "%u bytes, "
        "sequence=%llu, "
        "import=%.3f W, "
        "export=%.3f W, "
        "net=%.3f W\n",
        static_cast<unsigned int>(sizeof(packet)),
        static_cast<unsigned long long>(sequence_),
        metrics.importedPower,
        metrics.exportedPower,
        metrics.netPower
    );
}


// =============================================================================
// Status
// =============================================================================

bool UDPClient::ready() const
{
    return ready_;
}


// =============================================================================
// Server IP
// =============================================================================

IPAddress UDPClient::serverIP() const
{
    return serverIP_;
}