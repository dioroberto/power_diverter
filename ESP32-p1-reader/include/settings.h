#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "secrets.h"
#include "secrets_udp.h"
#include "udp_config.h"


namespace Settings
{
    

    // ========================================================
    // P1
    // ========================================================

    namespace P1
    {
        // ----------------------------------------------------
        // UART
        // ----------------------------------------------------

        constexpr int RX_PIN =
            16;

        // P1 is receive-only.
        constexpr int TX_PIN =
            -1;

        constexpr uint32_t BAUDRATE =
            115200;

        constexpr bool RX_INVERTED =
            true;


        // ----------------------------------------------------
        // Parser
        // ----------------------------------------------------

        constexpr size_t BUFFER_SIZE =
            4096;

        constexpr uint32_t TELEGRAM_TIMEOUT_MS =
            5000;


        // ----------------------------------------------------
        // Debug
        //
        // 0 = disabled
        // 1 = telegram status / CRC
        // 2 = complete telegram
        // 3 = every received byte
        // ----------------------------------------------------

        constexpr uint8_t DEBUG_LEVEL =
            1;
    }


    // ========================================================
    // UDP
    // ========================================================

    namespace UDP
    {
        
        // ----------------------------------------------------
        // mDNS resolution
        // ----------------------------------------------------

        constexpr uint32_t RESOLVE_INTERVAL_MS =
            10UL * 1000UL;


        // ----------------------------------------------------
        // Periodic re-resolution
        //
        // Detects a changed server IP address.
        // ----------------------------------------------------

        constexpr uint32_t RERESOLVE_INTERVAL_MS =
            5UL * 60UL * 1000UL;
    }


    // ========================================================
    // MQTT over WebSocket Secure
    // ========================================================

    namespace MQTT
    {
        // ----------------------------------------------------
        // WSS endpoint
        // ----------------------------------------------------

        constexpr const char* HOST =
            "push.dionisiotech.com";

        constexpr uint16_t PORT =
            443;

        constexpr const char* PATH =
            "/mqtt";

        constexpr const char* PROTOCOL =
            "mqtt";


        // ----------------------------------------------------
        // MQTT credentials
        //
        // Stored separately in secrets.h
        // ----------------------------------------------------

        constexpr const char* USERNAME =
            Secrets::MQTT::USERNAME;

        constexpr const char* PASSWORD =
            Secrets::MQTT::PASSWORD;


        // ----------------------------------------------------
        // MQTT topics
        // ----------------------------------------------------
        //
        // Device::NAME is the single source of identity.
        //
        // Topic layout:
        //
        //     my_device/P1/data
        //     my_device/status
        //     my_device/status/p1
        //     my_device/command
        //     my_device/config
        //     my_device/availability
        //
        // ----------------------------------------------------

        namespace Topic
        {
            inline String DATA()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/P1/data";
            }


            inline String STATUS()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/status";
            }


            inline String P1_STATUS()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/status/p1";
            }


            inline String COMMAND()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/command";
            }


            inline String CONFIG()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/config";
            }


            inline String AVAILABILITY()
            {
                return String(UDPConfig::DEVICE_NAME) +
                       "/availability";
            }
        }


        // ----------------------------------------------------
        // Periodic P1 publication
        // ----------------------------------------------------
        //
        // Every completed P1 telegram:
        //
        //     -> UDP immediately
        //
        // MQTT:
        //
        //     -> periodic snapshot
        //
        // ----------------------------------------------------

        constexpr uint32_t PUBLISH_INTERVAL_MS =
            5UL * 60UL * 1000UL;


        // ----------------------------------------------------
        // Initial P1 publication
        // ----------------------------------------------------
        //
        // true:
        //     Publish the first valid P1 snapshot as soon as
        //     MQTT becomes connected.
        //
        // false:
        //     Wait for PUBLISH_INTERVAL_MS.
        //
        // ----------------------------------------------------

        constexpr bool PUBLISH_FIRST_TELEGRAM =
            true;


        // ----------------------------------------------------
        // MQTT reconnect
        // ----------------------------------------------------
        //
        // Used when the WebSocket transport is available but
        // MQTT itself is disconnected.
        //
        // WebSocket reconnection is handled independently by
        // WebSocketsClient.
        //
        // ----------------------------------------------------

        constexpr uint32_t RECONNECT_INTERVAL_MS =
            5000;
    }

    // ========================================================
    // Application
    // ========================================================

    namespace Application
    {
        constexpr uint32_t STATUS_INTERVAL_MS =
            30000;

        constexpr uint32_t SERIAL_STARTUP_DELAY_MS =
            5000;
    }
}