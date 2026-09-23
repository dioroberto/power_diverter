#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <RNG.h>

#include "settings.h"
#include "secrets.h"
#include "secrets_udp.h"
#include "provisioner.h"
#include "P1Reader.h"
#include "UDPClient.h"
#include "MQTTClient.h"
#include "udp_config.h"


// =============================================================================
// Application configuration
// =============================================================================

namespace App
{
    constexpr uint8_t PROVISION_PIN = 27;
}


// =============================================================================
// Global clients
// =============================================================================

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
// Runtime state
// =============================================================================

namespace App
{
    bool haveP1Data = false;

    bool mqttFirstPublishPending = false;

    uint32_t lastP1TelegramMs = 0;

    uint32_t lastMqttPublishMs = 0;

    uint32_t lastP1HealthPublishMs = 0;
}


// =============================================================================
// Wi-Fi
// =============================================================================

bool setupWiFi()
{
    char ssid[32] = {};
    char pass[64] = {};


    // -------------------------------------------------------------------------
    // Provisioning input
    // -------------------------------------------------------------------------

    pinMode(
        App::PROVISION_PIN,
        INPUT_PULLUP
    );


    // -------------------------------------------------------------------------
    // Determine credentials
    // -------------------------------------------------------------------------

    if (digitalRead(App::PROVISION_PIN) == LOW)
    {
        Serial.println();
        Serial.println(
            "[WiFi] GPIO27 LOW: forcing provisioning."
        );


        WiFi.disconnect(true);


        provisioner.clear_creds();


        while (!provisioner.provision(ssid, pass))
        {
            Serial.println(
                "[WiFi] Provisioning failed. Retrying..."
            );
        }


        Serial.println(
            "[WiFi] Provisioning successful."
        );
    }
    else
    {
        Serial.println(
            "[WiFi] GPIO27 HIGH: using stored credentials."
        );


        if (!provisioner.get_creds(ssid, pass))
        {
            Serial.println(
                "[WiFi] No stored Wi-Fi credentials."
            );


            while (!provisioner.provision(ssid, pass))
            {
                Serial.println(
                    "[WiFi] Provisioning failed. Retrying..."
                );
            }
        }
    }


    // -------------------------------------------------------------------------
    // Configure station
    // -------------------------------------------------------------------------

    WiFi.mode(WIFI_STA);


    WiFi.setHostname(
        UDPConfig::DEVICE_NAME
    );


    // -------------------------------------------------------------------------
    // Connect
    // -------------------------------------------------------------------------

    Serial.println();

    Serial.print(
        "[WiFi] Connecting to: "
    );

    Serial.println(
        ssid
    );


    WiFi.begin(
        ssid,
        pass
    );


    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);

        Serial.print(".");
    }


    Serial.println();

    Serial.println(
        "[WiFi] Connected."
    );


    Serial.print(
        "[WiFi] IP address: "
    );

    Serial.println(
        WiFi.localIP()
    );


    Serial.print(
        "[WiFi] RSSI: "
    );

    Serial.print(
        WiFi.RSSI()
    );

    Serial.println(
        " dBm"
    );


    return true;
}


// =============================================================================
// mDNS
// =============================================================================

bool setupMDNS()
{
    Serial.println();

    Serial.println(
        "[mDNS] Starting..."
    );


    if (!MDNS.begin(UDPConfig::DEVICE_NAME))
    {
        Serial.println(
            "[mDNS] ERROR: Failed to start responder."
        );

        return false;
    }


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
        "========================================"
    );
    Serial.println(
        "[MQTT] Incoming message"
    );
    Serial.println(
        "========================================"
    );


    Serial.print("Topic: ");
    Serial.println(topic);


    Serial.print("Payload: ");

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
        const char c = value[i];


        switch (c)
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
                output += c;
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
        if (i != 0)
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
// P1 health
// =============================================================================

void publishP1Health()
{
    if (!mqttClient.connected())
    {
        return;
    }

    const uint32_t now = millis();

    uint32_t telegramAge = 0;

    if (App::haveP1Data)
    {
        telegramAge =
            now - App::lastP1TelegramMs;
    }

    String payload;

    payload.reserve(512);

    payload += "{";

    payload += "\"available\":";
    payload += App::haveP1Data ? "true" : "false";

    payload += ",\"telegram_age_ms\":";
    payload += String(telegramAge);

    payload += ",\"value_count\":";
    payload += String(
        p1Reader.getValueCount()
    );

    payload += ",\"last_telegram_ms\":";
    payload += String(
        App::lastP1TelegramMs
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
    payload += p1Reader.isReceiving()
        ? "true"
        : "false";

    payload += ",\"last_received_crc\":\"";
    payload += String(
        p1Reader.getLastReceivedCrc(),
        HEX
    );
    payload += "\"";

    payload += ",\"last_calculated_crc\":\"";
    payload += String(
        p1Reader.getLastCalculatedCrc(),
        HEX
    );
    payload += "\"";

    payload += ",\"last_telegram_length\":";
    payload += String(
        p1Reader.getLastTelegramLength()
    );

    payload += "\"";

    payload += "}";

    mqttClient.publish(
    Settings::MQTT::Topic::STATUS().c_str(),
    payload.c_str(),
    false
    );
}

// =============================================================================
// MQTT data scheduler
// =============================================================================

void processMqttData()
{
    if (!App::haveP1Data)
        return;


    const uint32_t now =
        millis();


    // -------------------------------------------------------------------------
    // Initial publication
    // -------------------------------------------------------------------------

    if (App::mqttFirstPublishPending)
    {
        if (!mqttClient.connected())
            return;


        String payload =
            buildMqttPayload();


        if (mqttClient.publishP1(
                payload.c_str(),
                false))
        {
            App::mqttFirstPublishPending =
                false;


            App::lastMqttPublishMs =
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
    // Periodic publication timer
    // -------------------------------------------------------------------------

    if (
        static_cast<uint32_t>(
            now - App::lastMqttPublishMs
        ) <
        Settings::MQTT::PUBLISH_INTERVAL_MS
    )
    {
        return;
    }


    // Advance the timer BEFORE checking MQTT.
    //
    // This deliberately prevents a disconnected MQTT client from causing
    // a tight retry loop.
    App::lastMqttPublishMs =
        now;


    if (!mqttClient.connected())
    {
        Serial.println(
            "[MQTT] Periodic snapshot skipped: "
            "MQTT not connected."
        );

        return;
    }


    String payload =
        buildMqttPayload();


    if (mqttClient.publishP1(
            payload.c_str(),
            false))
    {
        Serial.println();
        Serial.println(
            "========================================"
        );
        Serial.println(
            "[MQTT] Periodic P1 snapshot"
        );
        Serial.println(
            "========================================"
        );


        Serial.print(
            "Topic: "
        );

        Serial.println(
            mqttClient.p1DataTopic()
        );


        Serial.print(
            "Payload size: "
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
// P1 processing
// =============================================================================
//
// P1 is the primary real-time path.
//
//     P1 telegram
//         |
//         +--> application state
//         |
//         +--> UDP
//
// MQTT publication is deliberately not performed here.
// =============================================================================

void processP1()
{
    if (!p1Reader.available())
        return;


    const uint32_t now =
        millis();


    const bool firstP1Telegram =
        !App::haveP1Data;


    // -------------------------------------------------------------------------
    // Update P1 state
    // -------------------------------------------------------------------------

    App::haveP1Data = true;

    App::lastP1TelegramMs = now;


    // -------------------------------------------------------------------------
    // Schedule first MQTT publication
    // -------------------------------------------------------------------------

    if (
        firstP1Telegram &&
        Settings::MQTT::PUBLISH_FIRST_TELEGRAM
    )
    {
        App::mqttFirstPublishPending = true;
    }


    // -------------------------------------------------------------------------
    // UDP
    // -------------------------------------------------------------------------
    //
    // UDP is independent from MQTT.
    //
    // If the server has not yet been resolved, UDPClient simply skips the
    // packet. It does not block P1 processing.
    // -------------------------------------------------------------------------

    if (udpClient.ready())
    {
        udpClient.sendNetPower(
            p1Reader
        );
    }
    else
    {
        Serial.println(
            "[UDP] P1 received, but destination is not ready."
        );
    }


    // -------------------------------------------------------------------------
    // Diagnostic
    // -------------------------------------------------------------------------

    if (Settings::P1::DEBUG_LEVEL >= 1)
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
// Periodic P1 health
// =============================================================================

void processPeriodicHealth()
{
    if (!mqttClient.connected())
        return;


    const uint32_t now =
        millis();


    if (
        static_cast<uint32_t>(
            now - App::lastP1HealthPublishMs
        ) <
        Settings::Application::STATUS_INTERVAL_MS
    )
    {
        return;
    }


    App::lastP1HealthPublishMs =
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


    Serial.print("Device: ");
    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    Serial.print("P1 RX: ");
    Serial.println(
        Settings::P1::RX_PIN
    );


    Serial.print("P1 TX: ");
    Serial.println(
        Settings::P1::TX_PIN
    );


    Serial.print("P1 baudrate: ");
    Serial.println(
        Settings::P1::BAUDRATE
    );


    Serial.print("P1 inverted: ");
    Serial.println(
        Settings::P1::RX_INVERTED
            ? "yes"
            : "no"
    );


    Serial.print("UDP server: ");
    Serial.println(
        UDPConfig::SERVER_NAME
    );


    Serial.print("UDP port: ");
    Serial.println(
        UDPConfig::SERVER_PORT
    );


    Serial.print("UDP ready: ");
    Serial.println(
        udpClient.ready()
            ? "yes"
            : "no"
    );


    if (udpClient.ready())
    {
        Serial.print("UDP destination: ");
        Serial.print(
            udpClient.serverIP()
        );

        Serial.print(":");
        Serial.println(
            UDPConfig::SERVER_PORT
        );
    }


    Serial.print("MQTT server: ");
    Serial.print(
        Settings::MQTT::HOST
    );

    Serial.print(":");

    Serial.println(
        Settings::MQTT::PORT
    );


    Serial.print("MQTT client ID: ");
    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    Serial.print("MQTT P1 topic: ");
    Serial.println(
        mqttClient.p1DataTopic()
    );


    Serial.print("MQTT availability topic: ");
    Serial.println(
        Settings::MQTT::Topic::AVAILABILITY()
    );


    Serial.print("MQTT publish interval: ");
    Serial.print(
        Settings::MQTT::PUBLISH_INTERVAL_MS /
        1000UL
    );

    Serial.println(" seconds");


    Serial.print("P1 health interval: ");
    Serial.print(
        Settings::Application::STATUS_INTERVAL_MS /
        1000UL
    );

    Serial.println(" seconds");


    Serial.println();
}


// =============================================================================
// Setup
// =============================================================================

void setup()
{
    Serial.begin(115200);


    // -------------------------------------------------------------------------
    // Give the serial monitor time to attach.
    // -------------------------------------------------------------------------

    delay(
        Settings::Application::SERIAL_STARTUP_DELAY_MS
    );


    // -------------------------------------------------------------------------
    // Startup banner
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println(
        "========================================"
    );
    Serial.println(
        "           ESP32 P1 GATEWAY"
    );
    Serial.println(
        "========================================"
    );


    Serial.print("Device: ");
    Serial.println(
        UDPConfig::DEVICE_NAME
    );


    // =========================================================================
    // Wi-Fi
    // =========================================================================

    if (!setupWiFi())
    {
        Serial.println(
            "[APP] ERROR: Wi-Fi setup failed."
        );

        return;
    }


    // =========================================================================
    // mDNS
    // =========================================================================

    const bool mdnsReady =
        setupMDNS();


    if (!mdnsReady)
    {
        Serial.println(
            "[APP] WARNING: mDNS unavailable."
        );
    }


    // =========================================================================
    // UDP
    // =========================================================================
    //
    // UDP starts independently of server availability.
    //
    // begin() must NOT require the remote UDP server to be online.
    // =========================================================================

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


    // =========================================================================
    // P1
    // =========================================================================

    p1Reader.begin();


    Serial.println(
        "[APP] P1 reader started."
    );


    // =========================================================================
    // MQTT
    // =========================================================================

    mqttClient.setMessageCallback(
        onMQTTMessage
    );


    mqttClient.begin();


    Serial.println(
        "[APP] MQTT client started."
    );


    // =========================================================================
    // Timers
    // =========================================================================

    const uint32_t now =
        millis();


    App::lastMqttPublishMs =
        now;


    App::lastP1HealthPublishMs =
        now;


    // =========================================================================
    // Configuration
    // =========================================================================

    printConfiguration();


    // =========================================================================
    // Ready
    // =========================================================================

    Serial.println(
        "========================================"
    );

    Serial.println(
        "           DEVICE READY"
    );

    Serial.println(
        "========================================"
    );
}


// =============================================================================
// Main loop
// =============================================================================

void loop()
{
    // =========================================================================
    // 1. Cryptographic RNG
    // =========================================================================

    RNG.loop();


    // =========================================================================
    // 2. P1
    // =========================================================================
    //
    // Keep the meter reader serviced continuously.
    // =========================================================================

    p1Reader.loop();


    // =========================================================================
    // 3. UDP maintenance
    // =========================================================================
    //
    // Performs:
    //
    //     - destination resolution
    //     - retry resolution
    //     - periodic re-resolution
    //
    // UDP does not depend on MQTT.
    // =========================================================================

    udpClient.loop();


    // =========================================================================
    // 4. Process completed P1 telegram
    // =========================================================================

    processP1();


    // =========================================================================
    // 5. MQTT transport
    // =========================================================================
    //
    // Keep this serviced independently from P1 and UDP.
    // =========================================================================

    mqttClient.loop();


    // =========================================================================
    // 6. MQTT data scheduler
    // =========================================================================

    processMqttData();


    // =========================================================================
    // 7. P1 health scheduler
    // =========================================================================

    processPeriodicHealth();
}