#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "provisioner.h"
#include "UDPServer.h"
#include "server_config.h"
#include "server_keys.h"
#include "udp_config.h"

namespace Device
{
    constexpr uint8_t PROVISION_PIN = 27;
}

UDPServer udpServer(
    UDPConfig::SERVER_PORT,
    ServerSecrets::SERVER_PRIVATE_KEY,
    ServerSecrets::ESP32_P1_PUBLIC_KEY,
    UDPConfig::DEVICE_NAME
);

bool setupWiFi()
{
    char ssid[32];
    char pass[64];

    pinMode(Device::PROVISION_PIN, INPUT_PULLUP);

    if (digitalRead(Device::PROVISION_PIN) == LOW)
    {
        Serial.println("Provisioning requested");

        WiFi.disconnect(true);
        delay(100);

        provisioner.clear_creds();

        while (!provisioner.provision(ssid, pass))
        {
            Serial.println("Provisioning failed, retrying...");
            delay(1000);
        }
    }
    else
    {
        if (!provisioner.get_creds(ssid, pass))
        {
            Serial.println("No WiFi credentials stored");

            while (!provisioner.provision(ssid, pass))
            {
                Serial.println("Provisioning failed, retrying...");
                delay(1000);
            }
        }
    }

    WiFi.setHostname(UDPConfig::SERVER_NAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32 UDP Server");
    Serial.println("================================");

    if (!setupWiFi())
    {
        Serial.println("ERROR: WiFi setup failed");
        return;
    }

    if (!MDNS.begin(UDPConfig::SERVER_NAME))
    {
        Serial.println("ERROR: mDNS failed");
    }
    else
    {
        Serial.printf(
            "mDNS: %s.local\n",
            UDPConfig::SERVER_NAME
        );
    }

    if (!udpServer.begin())
    {
        Serial.println("ERROR: UDP server failed to start");
        return;
    }

    Serial.printf(
        "UDP server listening on port %u\n",
        UDPConfig::SERVER_PORT
    );

    Serial.printf(
        "Accepting device: %s\n",
        UDPConfig::DEVICE_NAME
    );
}

void loop()
{
    udpServer.loop();
}