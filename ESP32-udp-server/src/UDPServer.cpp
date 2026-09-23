#include "UDPServer.h"

#include <cstring>


// ============================================================
// Constructor
// ============================================================

UDPServer::UDPServer(
    uint16_t port,
    const uint8_t serverPrivateKey[32],
    const uint8_t devicePublicKey[32],
    const char* deviceName)
    : port_(port),
      initialized_(false),
      packetsReceived_(0),
      packetsAccepted_(0),
      packetsRejected_(0)
{
    crypto_.begin(
        serverPrivateKey,
        devicePublicKey,
        deviceName
    );
}


// ============================================================
// Begin
// ============================================================

bool UDPServer::begin()
{
    Serial.println();
    Serial.print("Starting UDP server on port ");
    Serial.println(port_);


    if (!crypto_.ready())
    {
        Serial.println(
            "ERROR: UDP crypto initialization failed."
        );

        return false;
    }


    if (!udp_.begin(port_))
    {
        Serial.println(
            "ERROR: udp.begin() failed."
        );

        return false;
    }


    initialized_ = true;


    Serial.println(
        "UDP server started."
    );

    Serial.print(
        "Listening for device: "
    );

    Serial.println(
        crypto_.deviceName()
    );


    return true;
}


// ============================================================
// Loop
// ============================================================

void UDPServer::loop()
{
    if (!initialized_)
        return;

    const int packetSize =
        udp_.parsePacket();

    if (packetSize <= 0)
        return;


    packetsReceived_++;

    processPacket();
}


// ============================================================
// Process packet
// ============================================================

void UDPServer::processPacket()
{
    uint8_t buffer[
        UDPCrypto::PACKET_SIZE
    ];


    const int packetSize =
        udp_.read(
            buffer,
            sizeof(buffer)
        );


    if (packetSize !=
        static_cast<int>(
            UDPCrypto::PACKET_SIZE))
    {
        Serial.printf(
            "UDP: invalid packet size: %d "
            "(expected %u)\n",
            packetSize,
            static_cast<unsigned int>(
                UDPCrypto::PACKET_SIZE
            )
        );

        packetsRejected_++;

        return;
    }


    const IPAddress remoteIP =
        udp_.remoteIP();

    const uint16_t remotePort =
        udp_.remotePort();


    UDPCrypto::Packet packet;


    if (!crypto_.decrypt(
            buffer,
            packetSize,
            packet))
    {
        Serial.print(
            "UDP: authentication/decryption "
            "failed from "
        );

        Serial.print(remoteIP);

        Serial.print(":");

        Serial.println(remotePort);

        packetsRejected_++;

        return;
    }


    packetsAccepted_++;


    printPacket(
        packet,
        remoteIP,
        remotePort
    );
}


// ============================================================
// Print decoded packet
// ============================================================

void UDPServer::printPacket(
    const UDPCrypto::Packet& packet,
    const IPAddress& remoteIP,
    uint16_t remotePort)
{
    Serial.printf(
        "UDP: device=%s "
        "seq=%llu "
        "timestamp=%llu ms "
        "net=%.3f kW "
        "from=%s:%u\n",
        crypto_.deviceName(),
        static_cast<unsigned long long>(
            packet.sequence
        ),
        static_cast<unsigned long long>(
            packet.timestampMs
        ),
        packet.netPower,
        remoteIP.toString().c_str(),
        remotePort
    );
}


// ============================================================
// Ready
// ============================================================

bool UDPServer::ready() const
{
    return initialized_;
}


// ============================================================
// Statistics
// ============================================================

uint32_t UDPServer::packetsReceived() const
{
    return packetsReceived_;
}


uint32_t UDPServer::packetsAccepted() const
{
    return packetsAccepted_;
}


uint32_t UDPServer::packetsRejected() const
{
    return packetsRejected_;
}