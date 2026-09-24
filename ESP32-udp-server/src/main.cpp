#include <Arduino.h>
#include <ESPmDNS.h>
#include <RNG.h>

#include "WiFiCompat.h"
#include "WiFiProvisioning.h"

#include "UDPServer.h"
#include "server_config.h"
#include "server_keys.h"
#include "udp_config.h"


// =============================================================================
// Application configuration
// =============================================================================

namespace AppConfig
{
    constexpr uint8_t PROVISION_PIN = 27;

    constexpr char WIFI_PROVISIONING_AP[] =
        "ESP32-UDP-Setup";
}


// =============================================================================
// Application state
// =============================================================================

namespace App
{
    struct State
    {
        bool wifiConnected = false;
        bool servicesStarted = false;
        bool mdnsAvailable = false;
    };

    State state;
}


// =============================================================================
// Global services
// =============================================================================

WiFiProvisioning wifi(
    AppConfig::WIFI_PROVISIONING_AP,
    AppConfig::PROVISION_PIN
);


UDPServer udpServer(
    UDPConfig::SERVER_PORT,
    ServerSecrets::SERVER_PRIVATE_KEY,
    ServerSecrets::ESP32_P1_PUBLIC_KEY,
    UDPConfig::DEVICE_NAME
);


// =============================================================================
// Forward declarations
// =============================================================================

// Wi-Fi
void startWiFi();
void processWiFi();

// mDNS
bool startMDNS();
void stopMDNS();

// Services
void startServices();
void processServices();

// Diagnostics
void printConfiguration();


// =============================================================================
// Wi-Fi
// =============================================================================

void startWiFi()
{
    Serial.println();
    Serial.println(
        "[WiFi] Starting..."
    );


    wifi.setHostname(
        UDPConfig::SERVER_NAME
    );


    /*
     * wifi.begin() may return false when:
     *
     * - provisioning AP has been started
     * - credentials are being connected
     * - Wi-Fi is not connected yet
     *
     * This is not a fatal error.
     *
     * WiFiProvisioning::loop() will handle the
     * connection/retry process.
     */

    if (wifi.begin())
    {
        App::state.wifiConnected = true;

        Serial.println(
            "[WiFi] Connected."
        );

        Serial.print(
            "[WiFi] IP address: "
        );

        Serial.println(
            wifi.localIP()
        );

        return;
    }


    Serial.println(
        "[WiFi] Not connected yet."
    );

    Serial.println(
        "[WiFi] Waiting for connection..."
    );
}


// =============================================================================
// Process Wi-Fi
// =============================================================================

void processWiFi()
{
    wifi.loop();


    const bool connected =
        wifi.connected();


    // -------------------------------------------------------------------------
    // Connection established
    // -------------------------------------------------------------------------

    if (
        connected &&
        !App::state.wifiConnected
    )
    {
        App::state.wifiConnected = true;

        Serial.println();
        Serial.println(
            "[WiFi] Connected."
        );

        Serial.print(
            "[WiFi] IP address: "
        );

        Serial.println(
            wifi.localIP()
        );
    }


    // -------------------------------------------------------------------------
    // Start services after initial connection
    // -------------------------------------------------------------------------

    if (
        connected &&
        !App::state.servicesStarted
    )
    {
        startServices();
    }


    // -------------------------------------------------------------------------
    // Connection lost
    // -------------------------------------------------------------------------

    if (
        !connected &&
        App::state.wifiConnected
    )
    {
        App::state.wifiConnected = false;

        Serial.println();
        Serial.println(
            "[WiFi] Connection lost."
        );


        if (App::state.servicesStarted)
        {
            Serial.println(
                "[APP] Existing services remain running."
            );

            Serial.println(
                "[APP] Waiting for Wi-Fi recovery..."
            );


            /*
             * UDPServer remains alive.
             *
             * Only mDNS is explicitly stopped because
             * it is tied to the current network interface.
             */
            stopMDNS();
        }
    }


    // -------------------------------------------------------------------------
    // Wi-Fi recovered
    // -------------------------------------------------------------------------

    if (
        connected &&
        App::state.servicesStarted &&
        !App::state.mdnsAvailable
    )
    {
        Serial.println();
        Serial.println(
            "[WiFi] Network recovered."
        );

        startMDNS();
    }
}


// =============================================================================
// mDNS
// =============================================================================

bool startMDNS()
{
    if (App::state.mdnsAvailable)
        return true;


    Serial.println();
    Serial.println(
        "[mDNS] Starting..."
    );


    if (!MDNS.begin(
            UDPConfig::SERVER_NAME
        ))
    {
        Serial.println(
            "[mDNS] ERROR: Failed to start responder."
        );

        App::state.mdnsAvailable = false;

        return false;
    }


    App::state.mdnsAvailable = true;


    Serial.print(
        "[mDNS] Hostname: "
    );

    Serial.print(
        UDPConfig::SERVER_NAME
    );

    Serial.println(
        ".local"
    );


    return true;
}


void stopMDNS()
{
    if (!App::state.mdnsAvailable)
        return;


    Serial.println(
        "[mDNS] Stopping..."
    );


    MDNS.end();

    App::state.mdnsAvailable = false;
}


// =============================================================================
// Start services
// =============================================================================
//
// Services are initialized exactly once.
//
// Wi-Fi must be connected before this function is called.
//
// A later Wi-Fi outage does not recreate the UDP server.
// =============================================================================

void startServices()
{
    if (App::state.servicesStarted)
        return;


    if (!wifi.connected())
    {
        Serial.println(
            "[APP] Cannot start services: "
            "Wi-Fi not connected."
        );

        return;
    }


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "[APP] Starting services"
    );

    Serial.println(
        "========================================"
    );


    // -------------------------------------------------------------------------
    // mDNS
    // -------------------------------------------------------------------------

    if (!startMDNS())
    {
        /*
         * mDNS is useful but not required for the UDP
         * server itself.
         */
        Serial.println(
            "[APP] WARNING: mDNS unavailable."
        );
    }


    // -------------------------------------------------------------------------
    // UDP server
    // -------------------------------------------------------------------------

    if (!udpServer.begin())
    {
        Serial.println(
            "[APP] ERROR: UDP server failed to start."
        );

        return;
    }


    Serial.printf(
        "[APP] UDP server listening on port %u\n",
        static_cast<unsigned int>(
            UDPConfig::SERVER_PORT
        )
    );


    // -------------------------------------------------------------------------
    // Application state
    // -------------------------------------------------------------------------

    App::state.servicesStarted = true;


    printConfiguration();


    Serial.println(
        "========================================"
    );

    Serial.println(
        "[APP] SERVER READY"
    );

    Serial.println(
        "========================================"
    );
}


// =============================================================================
// Process services
// =============================================================================

void processServices()
{
    /*
     * RNG is used by the encrypted UDP protocol.
     */
    RNG.loop();


    /*
     * UDPServer owns the UDP socket and packet processing.
     *
     * It deliberately does not know about Wi-Fi.
     */
    udpServer.loop();
}


// =============================================================================
// Diagnostics
// =============================================================================

void printConfiguration()
{
    Serial.println();
    Serial.println(
        "----------------------------------------"
    );

    Serial.println(
        "Configuration"
    );

    Serial.println(
        "----------------------------------------"
    );


    // -------------------------------------------------------------------------
    // Device
    // -------------------------------------------------------------------------

    Serial.print(
        "Device: "
    );

    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    // -------------------------------------------------------------------------
    // Wi-Fi
    // -------------------------------------------------------------------------

    Serial.print(
        "WiFi SSID: "
    );

    Serial.println(
        wifi.ssid()
    );

    Serial.print(
        "WiFi IP: "
    );

    Serial.println(
        wifi.localIP()
    );

    Serial.print(
        "WiFi RSSI: "
    );

    Serial.print(
        wifi.rssi()
    );

    Serial.println(
        " dBm"
    );


    // -------------------------------------------------------------------------
    // UDP
    // -------------------------------------------------------------------------

    Serial.print(
        "UDP hostname: "
    );

    Serial.println(
        UDPConfig::SERVER_NAME
    );

    Serial.print(
        "UDP port: "
    );

    Serial.println(
        UDPConfig::SERVER_PORT
    );

    Serial.print(
        "Accepted device: "
    );

    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    // -------------------------------------------------------------------------
    // mDNS
    // -------------------------------------------------------------------------

    Serial.print(
        "mDNS: "
    );

    Serial.println(
        App::state.mdnsAvailable
            ? "available"
            : "unavailable"
    );


    Serial.println();
}


// =============================================================================
// Setup
// =============================================================================

void setup()
{
    Serial.begin(
        115200
    );

    delay(1000);


    // -------------------------------------------------------------------------
    // Startup banner
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "           ESP32 UDP SERVER"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Device: "
    );

    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    // -------------------------------------------------------------------------
    // Wi-Fi
    // -------------------------------------------------------------------------
    //
    // WiFiProvisioning handles:
    //
    //   - provisioning pin
    //   - stored credentials
    //   - provisioning AP
    //   - Wi-Fi connection
    //   - reconnect attempts
    //   - hostname
    //
    // Services are deliberately NOT started here.
    // -------------------------------------------------------------------------

    startWiFi();


    Serial.println();
    Serial.println(
        "[APP] Startup complete."
    );


    if (!wifi.connected())
    {
        Serial.println(
            "[APP] Waiting for Wi-Fi before "
            "starting services."
        );
    }
}


// =============================================================================
// Main loop
// =============================================================================

void loop()
{
    // -------------------------------------------------------------------------
    // 1. Process Wi-Fi
    // -------------------------------------------------------------------------

    processWiFi();


    // -------------------------------------------------------------------------
    // 2. Wait for initial Wi-Fi connection
    // -------------------------------------------------------------------------

    if (!App::state.servicesStarted)
        return;


    // -------------------------------------------------------------------------
    // 3. Process services
    // -------------------------------------------------------------------------

    processServices();
}