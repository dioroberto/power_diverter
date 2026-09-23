#pragma once

#include <stdint.h>

namespace ServerConfig
{

    /*
     * Maximum UDP packet size accepted by the server.
     */
    constexpr size_t MAX_PACKET_SIZE = 128;

    /*
     * Maximum number of milliseconds a packet timestamp may
     * differ from the previous accepted timestamp when using
     * the device uptime counter.
     *
     * This is deliberately not used as a security check yet,
     * because millis() wraps and the ESP32 can reboot.
     */
}