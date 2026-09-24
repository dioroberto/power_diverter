#include <Arduino.h>
#include <WiFi.h>

const char* ssid = "WiFimodem-023B7";
const char* password = "Evh8uxqkKqsV8WH";

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("WiFi status: %d\n", WiFi.status());
        delay(1000);
    }

    Serial.println("Connected!");
    Serial.println(WiFi.localIP());
}

void loop()
{
}