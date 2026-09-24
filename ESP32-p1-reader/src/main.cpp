#include <Arduino.h>
#include <ESPmDNS.h>
#include <RNG.h>

#include "settings.h"
#include "secrets.h"
#include "secrets_udp.h"
#include "P1Reader.h"
#include "UDPClient.h"
#include "MQTTClient.h"
#include "UDPProtocol.h"
#include "udp_config.h"
#include "WiFiProvisioning.h"


// =============================================================================
// Application configuration
// =============================================================================

namespace AppConfig
{
    constexpr uint8_t PROVISION_PIN = 27;
    constexpr char WIFI_PROVISIONING_AP[] = "ESP32-P1-Setup";
}


// =============================================================================
// Application state
// =============================================================================

namespace App
{
    struct State
    {
        bool servicesStarted = false;
        bool wifiConnected = false;
        bool mdnsAvailable = false;

        bool haveP1Data = false;
        bool mqttFirstPublishPending = false;

        uint32_t lastP1TelegramMs = 0;
        uint32_t lastMqttPublishMs = 0;
        uint32_t lastP1HealthPublishMs = 0;
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

P1Reader p1Reader(
    Serial2,
    Settings::P1::RX_PIN,
    Settings::P1::TX_PIN,
    Settings::P1::BAUDRATE,
    Settings::P1::TELEGRAM_TIMEOUT_MS
);

UDPClient udpClient(
    UDPConfig::SERVER_NAME,
    UDPConfig::SERVER_PORT
);

MQTTClient mqttClient(
    Settings::MQTT::HOST,
    Settings::MQTT::PORT,
    Settings::MQTT::PATH,
    Settings::MQTT::PROTOCOL,
    Settings::MQTT::USERNAME,
    Settings::MQTT::PASSWORD,
    UDPConfig::DEVICE_NAME
);


// =============================================================================
// Forward declarations
// =============================================================================

// Wi-Fi / network
void startWiFi();
void processWiFi();

bool startMDNS();
void stopMDNS();

// Services
void startServices();
void processServices();

// P1
void processP1();

// MQTT
void processMqtt();
void processMqttData();
void processPeriodicHealth();

void onMQTTMessage(
    const char* topic,
    const char* payload,
    unsigned int size
);

String buildMqttPayload();
void publishP1Health();

// JSON
void appendJsonEscaped(
    String& output,
    const String& value
);

// Diagnostics
void printConfiguration();


// =============================================================================
// Wi-Fi
// =============================================================================

void startWiFi()
{
    Serial.println();
    Serial.println("[WiFi] Starting...");

    wifi.setHostname(
        UDPConfig::DEVICE_NAME
    );

    if (!wifi.begin())
    {
        Serial.println(
            "[WiFi] Not connected yet."
        );

        Serial.println(
            "[WiFi] Waiting for connection..."
        );

        return;
    }

    App::state.wifiConnected = true;

    Serial.println(
        "[WiFi] Connected."
    );

    Serial.print(
        "[WiFi] IP: "
    );

    Serial.println(
        wifi.localIP()
    );
}


void processWiFi()
{
    wifi.loop();

    const bool connected =
        wifi.connected();


    // -------------------------------------------------------------------------
    // Wi-Fi connected
    // -------------------------------------------------------------------------

    if (connected && !App::state.wifiConnected)
    {
        App::state.wifiConnected = true;

        Serial.println();
        Serial.println(
            "[WiFi] Connected."
        );

        Serial.print(
            "[WiFi] IP: "
        );

        Serial.println(
            wifi.localIP()
        );
    }


    // -------------------------------------------------------------------------
    // Start services after first connection
    // -------------------------------------------------------------------------

    if (
        connected &&
        !App::state.servicesStarted
    )
    {
        startServices();
    }


    // -------------------------------------------------------------------------
    // Wi-Fi lost
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

            stopMDNS();

            udpClient.setMDNSAvailable(
                false
            );
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

        const bool mdnsReady =
            startMDNS();

        udpClient.setMDNSAvailable(
            mdnsReady
        );
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

    if (!MDNS.begin(UDPConfig::DEVICE_NAME))
    {
        Serial.println(
            "[mDNS] ERROR: Failed to start responder."
        );

        App::state.mdnsAvailable = false;

        return false;
    }

    App::state.mdnsAvailable = true;

    Serial.print(
        "[mDNS] Local hostname: "
    );

    Serial.print(
        UDPConfig::DEVICE_NAME
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
// Service startup
// =============================================================================
//
// Services are initialized exactly once.
//
// Wi-Fi must be connected before this function is called.
//
// A later Wi-Fi outage does not recreate the services.
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

    const bool mdnsReady =
        startMDNS();

    if (!mdnsReady)
    {
        Serial.println(
            "[APP] WARNING: mDNS unavailable."
        );
    }


    // -------------------------------------------------------------------------
    // UDP
    // -------------------------------------------------------------------------

    if (!udpClient.begin(mdnsReady))
    {
        Serial.println(
            "[APP] ERROR: UDP client initialization failed."
        );
    }
    else
    {
        Serial.println(
            "[APP] UDP client started."
        );
    }


    // -------------------------------------------------------------------------
    // P1
    // -------------------------------------------------------------------------

    p1Reader.begin();

    Serial.println(
        "[APP] P1 reader started."
    );


    // -------------------------------------------------------------------------
    // MQTT
    // -------------------------------------------------------------------------

    mqttClient.setMessageCallback(
        onMQTTMessage
    );

    mqttClient.begin();

    Serial.println(
        "[APP] MQTT client started."
    );


    // -------------------------------------------------------------------------
    // Timers
    // -------------------------------------------------------------------------

    const uint32_t now =
        millis();

    App::state.lastMqttPublishMs =
        now;

    App::state.lastP1HealthPublishMs =
        now;


    // -------------------------------------------------------------------------
    // Application state
    // -------------------------------------------------------------------------

    App::state.servicesStarted = true;

    printConfiguration();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "[APP] DEVICE READY"
    );

    Serial.println(
        "========================================"
    );
}


// =============================================================================
// Service processing
// =============================================================================

void processServices()
{
    RNG.loop();

    p1Reader.loop();

    udpClient.loop();

    processP1();

    mqttClient.loop();

    processMqtt();

    processPeriodicHealth();
}


// =============================================================================
// P1 processing
// =============================================================================
//
// P1 is the real-time path:
//
//     P1 telegram
//          |
//          +--> application state
//          |
//          +--> UDP
//
// MQTT publication is handled separately.
// =============================================================================

void processP1()
{
    if (!p1Reader.available())
        return;

    const uint32_t now =
        millis();

    const bool firstTelegram =
        !App::state.haveP1Data;


    // -------------------------------------------------------------------------
    // Application state
    // -------------------------------------------------------------------------

    App::state.haveP1Data = true;

    App::state.lastP1TelegramMs =
        now;


    // -------------------------------------------------------------------------
    // Initial MQTT publication
    // -------------------------------------------------------------------------

    if (
        firstTelegram &&
        Settings::MQTT::PUBLISH_FIRST_TELEGRAM
    )
    {
        App::state.mqttFirstPublishPending =
            true;
    }


    // -------------------------------------------------------------------------
    // UDP
    // -------------------------------------------------------------------------

    if (udpClient.ready())
    {
        udpClient.sendMetrics(
            p1Reader
        );
    }
    else
    {
        Serial.println(
            "[UDP] P1 received, but "
            "destination is not ready."
        );
    }


    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------

    if (
        Settings::P1::DEBUG_LEVEL >= 1
    )
    {
        Serial.print(
            "[P1] Telegram received. Values: "
        );

        Serial.println(
            p1Reader.getValueCount()
        );
    }
}


// =============================================================================
// MQTT processing
// =============================================================================

void processMqtt()
{
    processMqttData();
}


// =============================================================================
// MQTT callback
// =============================================================================

void onMQTTMessage(
    const char* topic,
    const char* payload,
    unsigned int size
)
{
    Serial.println();
    Serial.println(
        "[MQTT] Incoming message"
    );

    Serial.print(
        "[MQTT] Topic: "
    );

    Serial.println(
        topic
    );

    Serial.print(
        "[MQTT] Payload: "
    );

    Serial.printf(
        "%.*s\n",
        static_cast<int>(size),
        payload
    );
}


// =============================================================================
// JSON escaping
// =============================================================================

void appendJsonEscaped(
    String& output,
    const String& value
)
{
    for (size_t i = 0; i < value.length(); ++i)
    {
        switch (value[i])
        {
            case '"':
                output += "\\\"";
                break;

            case '\\':
                output += "\\\\";
                break;

            case '\b':
                output += "\\b";
                break;

            case '\f':
                output += "\\f";
                break;

            case '\n':
                output += "\\n";
                break;

            case '\r':
                output += "\\r";
                break;

            case '\t':
                output += "\\t";
                break;

            default:
                output += value[i];
                break;
        }
    }
}


// =============================================================================
// Build MQTT P1 payload
// =============================================================================

String buildMqttPayload()
{
    const size_t count =
        p1Reader.getValueCount();

    String payload;

    payload.reserve(
        count * 40 + 4
    );

    payload += '{';

    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            payload += ',';

        const P1Value& value =
            p1Reader.getValue(i);

        payload += '"';

        appendJsonEscaped(
            payload,
            value.obis
        );

        payload += "\":\"";

        appendJsonEscaped(
            payload,
            value.value
        );

        payload += '"';
    }

    payload += '}';

    return payload;
}


// =============================================================================
// MQTT P1 data scheduler
// =============================================================================

void processMqttData()
{
    if (!App::state.haveP1Data)
        return;

    const uint32_t now =
        millis();


    // -------------------------------------------------------------------------
    // Initial publication
    // -------------------------------------------------------------------------

    if (App::state.mqttFirstPublishPending)
    {
        if (!mqttClient.connected())
            return;

        const String payload =
            buildMqttPayload();

        if (
            mqttClient.publishP1(
                payload.c_str(),
                false
            )
        )
        {
            App::state.mqttFirstPublishPending =
                false;

            App::state.lastMqttPublishMs =
                now;

            Serial.println(
                "[MQTT] Initial P1 snapshot published."
            );

            Serial.print(
                "[MQTT] Topic: "
            );

            Serial.println(
                mqttClient.p1DataTopic()
            );
        }

        return;
    }


    // -------------------------------------------------------------------------
    // Periodic publication
    // -------------------------------------------------------------------------

    if (
        static_cast<uint32_t>(
            now -
            App::state.lastMqttPublishMs
        ) <
        Settings::MQTT::PUBLISH_INTERVAL_MS
    )
    {
        return;
    }


    // Advance timer before checking MQTT.
    //
    // This prevents a disconnected MQTT connection from causing
    // a tight retry loop.

    App::state.lastMqttPublishMs =
        now;


    if (!mqttClient.connected())
    {
        Serial.println(
            "[MQTT] Periodic snapshot skipped: "
            "MQTT not connected."
        );

        return;
    }


    const String payload =
        buildMqttPayload();

    if (
        mqttClient.publishP1(
            payload.c_str(),
            false
        )
    )
    {
        Serial.println();
        Serial.println(
            "[MQTT] Periodic P1 snapshot published."
        );

        Serial.print(
            "[MQTT] Topic: "
        );

        Serial.println(
            mqttClient.p1DataTopic()
        );

        Serial.print(
            "[MQTT] Payload size: "
        );

        Serial.print(
            payload.length()
        );

        Serial.println(
            " bytes"
        );
    }
    else
    {
        Serial.println(
            "[MQTT] Periodic snapshot failed."
        );
    }
}


// =============================================================================
// P1 health publication
// =============================================================================

void publishP1Health()
{
    if (!mqttClient.connected())
        return;

    const uint32_t now =
        millis();

    const uint32_t telegramAge =
        App::state.haveP1Data
            ? now - App::state.lastP1TelegramMs
            : 0;

    String payload;

    payload.reserve(512);

    payload += '{';

    payload += "\"available\":";
    payload +=
        App::state.haveP1Data
            ? "true"
            : "false";

    payload += ",\"telegram_age_ms\":";
    payload += String(
        telegramAge
    );

    payload += ",\"value_count\":";
    payload += String(
        p1Reader.getValueCount()
    );

    payload += ",\"last_telegram_ms\":";
    payload += String(
        App::state.lastP1TelegramMs
    );

    payload += ",\"rx_bytes\":";
    payload += String(
        p1Reader.getRxByteCount()
    );

    payload += ",\"telegram_starts\":";
    payload += String(
        p1Reader.getTelegramStartCount()
    );

    payload += ",\"telegram_ends\":";
    payload += String(
        p1Reader.getTelegramEndCount()
    );

    payload += ",\"valid\":";
    payload += String(
        p1Reader.getValidTelegramCount()
    );

    payload += ",\"invalid\":";
    payload += String(
        p1Reader.getInvalidTelegramCount()
    );

    payload += ",\"crc_errors\":";
    payload += String(
        p1Reader.getCrcErrorCount()
    );

    payload += ",\"timeouts\":";
    payload += String(
        p1Reader.getTelegramTimeoutCount()
    );

    payload += ",\"overflows\":";
    payload += String(
        p1Reader.getBufferOverflowCount()
    );

    payload += ",\"receiving\":";
    payload +=
        p1Reader.isReceiving()
            ? "true"
            : "false";

    payload += ",\"last_received_crc\":\"";

    payload += String(
        p1Reader.getLastReceivedCrc(),
        HEX
    );

    payload += '"';

    payload += ",\"last_calculated_crc\":\"";

    payload += String(
        p1Reader.getLastCalculatedCrc(),
        HEX
    );

    payload += '"';

    payload += ",\"last_telegram_length\":";
    payload += String(
        p1Reader.getLastTelegramLength()
    );

    payload += '}';


    mqttClient.publish(
        Settings::MQTT::Topic::STATUS().c_str(),
        payload.c_str(),
        false
    );
}


// =============================================================================
// Periodic health scheduler
// =============================================================================

void processPeriodicHealth()
{
    if (!mqttClient.connected())
        return;

    const uint32_t now =
        millis();

    if (
        static_cast<uint32_t>(
            now -
            App::state.lastP1HealthPublishMs
        ) <
        Settings::Application::STATUS_INTERVAL_MS
    )
    {
        return;
    }

    App::state.lastP1HealthPublishMs =
        now;

    publishP1Health();
}


// =============================================================================
// Startup diagnostics
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
    // P1
    // -------------------------------------------------------------------------

    Serial.print(
        "P1 RX: "
    );

    Serial.println(
        Settings::P1::RX_PIN
    );

    Serial.print(
        "P1 TX: "
    );

    Serial.println(
        Settings::P1::TX_PIN
    );

    Serial.print(
        "P1 baudrate: "
    );

    Serial.println(
        Settings::P1::BAUDRATE
    );

    Serial.print(
        "P1 inverted: "
    );

    Serial.println(
        Settings::P1::RX_INVERTED
            ? "yes"
            : "no"
    );


    // -------------------------------------------------------------------------
    // UDP
    // -------------------------------------------------------------------------

    Serial.print(
        "UDP server: "
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
        "UDP packet size: "
    );

    Serial.println(
        UDPCrypto::PACKET_SIZE
    );

    Serial.print(
        "UDP P1 metrics size: "
    );

    Serial.println(
        UDPProtocol::P1_METRICS_SIZE
    );

    Serial.print(
        "UDP ready: "
    );

    Serial.println(
        udpClient.ready()
            ? "yes"
            : "no"
    );

    if (udpClient.ready())
    {
        Serial.print(
            "UDP destination: "
        );

        Serial.print(
            udpClient.serverIP()
        );

        Serial.print(':');

        Serial.println(
            UDPConfig::SERVER_PORT
        );
    }


    // -------------------------------------------------------------------------
    // MQTT
    // -------------------------------------------------------------------------

    Serial.print(
        "MQTT server: "
    );

    Serial.print(
        Settings::MQTT::HOST
    );

    Serial.print(':');

    Serial.println(
        Settings::MQTT::PORT
    );

    Serial.print(
        "MQTT client ID: "
    );

    Serial.println(
        UDPConfig::DEVICE_NAME
    );

    Serial.print(
        "MQTT P1 topic: "
    );

    Serial.println(
        mqttClient.p1DataTopic()
    );

    Serial.print(
        "MQTT availability topic: "
    );

    Serial.println(
        Settings::MQTT::Topic::AVAILABILITY()
    );

    Serial.print(
        "MQTT publish interval: "
    );

    Serial.print(
        Settings::MQTT::PUBLISH_INTERVAL_MS /
        1000UL
    );

    Serial.println(
        " seconds"
    );


    // -------------------------------------------------------------------------
    // Health
    // -------------------------------------------------------------------------

    Serial.print(
        "P1 health interval: "
    );

    Serial.print(
        Settings::Application::STATUS_INTERVAL_MS /
        1000UL
    );

    Serial.println(
        " seconds"
    );

    Serial.println();
}


// =============================================================================
// Setup
// =============================================================================

void setup()
{
    Serial.begin(115200);

    delay(
        Settings::Application::SERIAL_STARTUP_DELAY_MS
    );


    // -------------------------------------------------------------------------
    // Banner
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "              P1 GATEWAY"
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
    processWiFi();

    if (!App::state.servicesStarted)
        return;

    processServices();
}