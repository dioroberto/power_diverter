#include "UDPClient.h"

#include "UDPProtocol.h"
#include "secrets.h"
#include "secrets_udp.h"
#include "udp_config.h"


// ============================================================
// Constructor
// ============================================================

UDPClient::UDPClient(
    const char* hostname,
    uint16_t port
)
    : hostname_(hostname),
      port_(port),
      serverIP_(INADDR_NONE),
      mdnsAvailable_(false),
      ready_(false),
      lastResolve_(0)
{
}


// ============================================================
// Begin
// ============================================================

bool UDPClient::begin(bool mdnsAvailable)
{
    mdnsAvailable_ = mdnsAvailable;

    ready_ = false;
    serverIP_ = INADDR_NONE;

    lastResolve_ = millis();


    // --------------------------------------------------------
    // Start local UDP socket.
    // --------------------------------------------------------

    if (!udp_.begin(0))
    {
        Serial.println(
            "UDP: failed to start socket."
        );

        return false;
    }


    // --------------------------------------------------------
    // Initialise cryptography.
    // --------------------------------------------------------

    if (!crypto_.begin(
            SecretsUDP::UDP::DEVICE_PRIVATE_KEY,
            SecretsUDP::UDP::SERVER_PUBLIC_KEY,
            UDPConfig::DEVICE_NAME))
    {
        Serial.println(
            "UDP: crypto initialization failed."
        );

        return false;
    }


    Serial.println(
        "UDP: client started."
    );

    Serial.println(
        "UDP: encryption enabled."
    );


    // --------------------------------------------------------
    // Initial resolution.
    // --------------------------------------------------------

    if (mdnsAvailable_)
    {
        resolveServer();
    }
    else
    {
        Serial.println(
            "UDP: mDNS unavailable; "
            "destination cannot currently be resolved."
        );
    }


    return true;
}


// ============================================================
// Loop
// ============================================================

void UDPClient::loop()
{
    // --------------------------------------------------------
    // If mDNS is unavailable, there is nothing to resolve.
    //
    // Importantly, this does NOT disable the UDP socket.
    // --------------------------------------------------------

    if (!mdnsAvailable_)
        return;


    const uint32_t now = millis();


    const uint32_t interval =
        ready_
            ? RERESOLVE_INTERVAL_MS
            : RESOLVE_INTERVAL_MS;


    if ((now - lastResolve_) < interval)
        return;


    lastResolve_ = now;

    resolveServer();
}


// ============================================================
// Resolve server
// ============================================================

bool UDPClient::resolveServer()
{
    if (!mdnsAvailable_)
        return false;


    Serial.printf(
        "UDP: resolving %s...\n",
        hostname_
    );


    const IPAddress resolved =
        MDNS.queryHost(hostname_);


    if (resolved == INADDR_NONE)
    {
        markUnavailable();

        Serial.printf(
            "UDP: server %s unavailable.\n",
            hostname_
        );

        return false;
    }


    const bool addressChanged =
        !ready_ ||
        serverIP_ != resolved;


    serverIP_ = resolved;
    ready_ = true;


    if (addressChanged)
    {
        Serial.printf(
            "UDP: server resolved: %s\n",
            serverIP_.toString().c_str()
        );

        Serial.printf(
            "UDP: destination %s:%u\n",
            serverIP_.toString().c_str(),
            static_cast<unsigned int>(port_)
        );
    }


    return true;
}


// ============================================================
// Mark unavailable
// ============================================================

void UDPClient::markUnavailable()
{
    ready_ = false;
    serverIP_ = INADDR_NONE;
}


// ============================================================
// Send P1 metrics
// ============================================================

void UDPClient::sendMetrics(
    const P1Reader& p1Reader
)
{
    // --------------------------------------------------------
    // No destination IP.
    //
    // Do NOT attempt to send.
    // --------------------------------------------------------

    if (!ready_)
    {
        Serial.println(
            "UDP: no destination; packet skipped."
        );

        return;
    }


    // --------------------------------------------------------
    // Build shared P1 metrics structure.
    //
    // This structure is defined in UDPProtocol.h and is also
    // used by the UDP receiver.
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Sequence.
    // --------------------------------------------------------

    static uint64_t sequence = 0;

    ++sequence;


    // --------------------------------------------------------
    // Monotonic device timestamp.
    // --------------------------------------------------------

    const uint64_t timestamp =
        static_cast<uint64_t>(millis());


    // --------------------------------------------------------
    // Allocate complete encrypted packet.
    // --------------------------------------------------------

    uint8_t packet[UDPCrypto::PACKET_SIZE];


    // --------------------------------------------------------
    // Encrypt.
    // --------------------------------------------------------

    if (!crypto_.encrypt(
            sequence,
            timestamp,
            reinterpret_cast<const uint8_t*>(&metrics),
            sizeof(metrics),
            packet,
            sizeof(packet)))
    {
        Serial.println(
            "UDP: encryption failed."
        );

        return;
    }


    // --------------------------------------------------------
    // Sanity check.
    // --------------------------------------------------------

    if (sizeof(packet) != UDPCrypto::PACKET_SIZE)
    {
        Serial.println(
            "UDP: internal packet size error."
        );

        return;
    }


    // --------------------------------------------------------
    // Begin packet.
    // --------------------------------------------------------

    if (!udp_.beginPacket(
            serverIP_,
            port_))
    {
        Serial.println(
            "UDP: beginPacket() failed."
        );

        // Keep the resolved address.
        //
        // A failed send does NOT prove that DNS/mDNS
        // resolution is wrong.
        return;
    }


    // --------------------------------------------------------
    // Write packet.
    // --------------------------------------------------------

    const size_t written =
        udp_.write(
            packet,
            sizeof(packet)
        );


    if (written != sizeof(packet))
    {
        Serial.printf(
            "UDP: write failed: %u/%u bytes\n",
            static_cast<unsigned int>(written),
            static_cast<unsigned int>(sizeof(packet))
        );

        // Finish/cancel current packet.
        udp_.endPacket();

        return;
    }


    // --------------------------------------------------------
    // Send.
    // --------------------------------------------------------

    if (!udp_.endPacket())
    {
        Serial.println(
            "UDP: endPacket() failed."
        );

        return;
    }


    // --------------------------------------------------------
    // Diagnostic.
    // --------------------------------------------------------

    Serial.printf(
        "UDP: encrypted P1 packet sent: "
        "%u bytes, "
        "sequence=%llu, "
        "import=%.3f W, "
        "export=%.3f W, "
        "net=%.3f W\n",
        static_cast<unsigned int>(sizeof(packet)),
        static_cast<unsigned long long>(sequence),
        metrics.importedPower,
        metrics.exportedPower,
        metrics.netPower
    );
}


// ============================================================
// Status
// ============================================================

bool UDPClient::ready() const
{
    return ready_;
}


// ============================================================
// Server IP
// ============================================================

IPAddress UDPClient::serverIP() const
{
    return serverIP_;
}