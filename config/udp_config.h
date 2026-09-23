#pragma once

#include <stdint.h>

namespace UDPConfig
{
    // --------------------------------------------------------
    // Device identity
    // --------------------------------------------------------

    constexpr const char* DEVICE_NAME =
        "mid-p1";

    // --------------------------------------------------------
    // UDP server identity
    // --------------------------------------------------------

    constexpr const char* SERVER_NAME =
        "mid-udp-server";

    // --------------------------------------------------------
    // UDP configuration
    // --------------------------------------------------------

    constexpr uint16_t SERVER_PORT =
        9001;
}