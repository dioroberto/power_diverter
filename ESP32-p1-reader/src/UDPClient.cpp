#include "UDPClient.h"

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
// Send net power
// ============================================================

void UDPClient::sendNetPower(
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
    // Obtain semantic value from P1Reader.
    // --------------------------------------------------------

    const float netPower =
        p1Reader.netPower();


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
    // Encrypt.
    // --------------------------------------------------------

    uint8_t packet[UDPCrypto::PACKET_SIZE];


    if (!crypto_.encrypt(
            sequence,
            timestamp,
            netPower,
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

        // Cancel/finish current packet.
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
        "UDP: encrypted packet sent: "
        "%u bytes, sequence=%llu, net=%.3f W\n",
        static_cast<unsigned int>(sizeof(packet)),
        static_cast<unsigned long long>(sequence),
        netPower
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