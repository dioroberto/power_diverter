#pragma once

/*
 * Platform-independent WiFi include.
 */

#if defined(ESP32)

    #include <WiFi.h>

#elif defined(ESP8266)

    #include <ESP8266WiFi.h>

#else

    #error "Unsupported platform: ESP32 or ESP8266 required"

#endif