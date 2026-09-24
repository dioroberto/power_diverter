#pragma once

#include <Arduino.h>

#if defined(ESP32)

#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>

#elif defined(ESP8266)

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#else

#error "Unsupported platform: ESP32 or ESP8266 required"

#endif


class WiFiProvisioning
{
public:

    WiFiProvisioning(
        const char *apName,
        uint8_t provisionPin = 255
    )
        : _apName(apName),
          _provisionPin(provisionPin)
    {
    }


    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    void setHostname(const char *hostname)
    {
        _hostname = hostname;
    }


    // -------------------------------------------------------------------------
    // Startup
    // -------------------------------------------------------------------------

    bool begin()
    {
        if (_provisionPin != 255)
        {
            pinMode(
                _provisionPin,
                INPUT_PULLUP
            );

            if (digitalRead(_provisionPin) == LOW)
            {
                Serial.println(
                    "[WiFi] Provisioning requested."
                );

                clearCredentials();

                startProvisioning();

                return false;
            }
        }


        if (!loadCredentials())
        {
            Serial.println(
                "[WiFi] No stored credentials."
            );

            startProvisioning();

            return false;
        }


        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        if (_hostname != nullptr)
            WiFi.setHostname(_hostname);


        startConnection();


        return connected();
    }


    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------

    void loop()
    {
        if (_provisioning)
        {
            handleProvisioning();
            return;
        }


        if (connected())
        {
            return;
        }


        const uint32_t now = millis();


        // Do not retry continuously.
        if (
            static_cast<uint32_t>(
                now - _lastConnectionAttemptMs
            ) <
            RETRY_INTERVAL_MS
        )
        {
            return;
        }


        startConnection();
    }


    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    bool connected() const
    {
        return WiFi.status() == WL_CONNECTED;
    }


    String ssid() const
    {
        return WiFi.SSID();
    }


    IPAddress localIP() const
    {
        return WiFi.localIP();
    }


    int32_t rssi() const
    {
        return WiFi.RSSI();
    }


    // -------------------------------------------------------------------------
    // Credentials
    // -------------------------------------------------------------------------

    void clearCredentials()
    {
#if defined(ESP32)

        Preferences preferences;

        preferences.begin(
            "wifi",
            false
        );

        preferences.clear();

        preferences.end();

#elif defined(ESP8266)

        EEPROM.begin(EEPROM_SIZE);

        for (size_t i = 0; i < EEPROM_SIZE; ++i)
            EEPROM.write(i, 0);

        EEPROM.commit();
        EEPROM.end();

#endif

        _ssid[0] = '\0';
        _password[0] = '\0';
    }


private:

    static constexpr uint32_t RETRY_INTERVAL_MS = 10000;

#if defined(ESP8266)

    static constexpr size_t EEPROM_SIZE = 128;

#endif


    const char *_apName;
    const char *_hostname = nullptr;

    uint8_t _provisionPin;


    char _ssid[33] = {};
    char _password[65] = {};


    bool _provisioning = false;
    bool _provisioningComplete = false;


    uint32_t _lastConnectionAttemptMs = 0;


    // =========================================================================
    // Connection
    // =========================================================================

    void startConnection()
    {
        if (_ssid[0] == '\0')
            return;


        _lastConnectionAttemptMs = millis();


        Serial.print(
            "[WiFi] Connecting to "
        );

        Serial.println(
            _ssid
        );


        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        if (_hostname != nullptr)
            WiFi.setHostname(_hostname);


        /*
         * Important:
         *
         * Do NOT use WiFi.disconnect(true) here.
         *
         * On ESP32 this can erase the stored configuration.
         */
        WiFi.disconnect(false);

        WiFi.begin(
            _ssid,
            _password
        );
    }


    // =========================================================================
    // Load credentials
    // =========================================================================

    bool loadCredentials()
    {
#if defined(ESP32)

        Preferences preferences;

        if (!preferences.begin("wifi", true))
            return false;


        const String ssid =
            preferences.getString(
                "ssid",
                ""
            );

        const String password =
            preferences.getString(
                "password",
                ""
            );


        preferences.end();


        if (ssid.length() == 0)
            return false;


        ssid.toCharArray(
            _ssid,
            sizeof(_ssid)
        );

        password.toCharArray(
            _password,
            sizeof(_password)
        );


        return true;

#elif defined(ESP8266)

        EEPROM.begin(EEPROM_SIZE);


        char ssid[33] = {};
        char password[65] = {};


        for (size_t i = 0; i < sizeof(ssid) - 1; ++i)
        {
            ssid[i] =
                static_cast<char>(
                    EEPROM.read(i)
                );
        }


        for (size_t i = 0; i < sizeof(password) - 1; ++i)
        {
            password[i] =
                static_cast<char>(
                    EEPROM.read(33 + i)
                );
        }


        EEPROM.end();


        if (ssid[0] == '\0')
            return false;


        strncpy(
            _ssid,
            ssid,
            sizeof(_ssid) - 1
        );

        strncpy(
            _password,
            password,
            sizeof(_password) - 1
        );


        return true;

#endif
    }


    // =========================================================================
    // Provisioning
    // =========================================================================

    void startProvisioning()
    {
        _provisioning = true;
        _provisioningComplete = false;


        WiFi.mode(WIFI_AP);

        WiFi.softAP(
            _apName
        );


        Serial.println(
            "[WiFi] Provisioning AP started."
        );

        Serial.print(
            "[WiFi] AP address: "
        );

        Serial.println(
            WiFi.softAPIP()
        );


        _server.on(
            "/",
            [this]()
            {
                handleRoot();
            }
        );


        _server.on(
            "/save",
            [this]()
            {
                handleSave();
            }
        );


        _server.begin();
    }


    void handleProvisioning()
    {
        _server.handleClient();


        if (!_provisioningComplete)
            return;


        _provisioning = false;


        _server.stop();

        WiFi.softAPdisconnect(true);


        WiFi.mode(WIFI_STA);

        WiFi.setAutoReconnect(true);


        startConnection();
    }


    void handleRoot()
    {
        _server.send(
            200,
            "text/html",
            "<html><body>"
            "<h2>ESP WiFi Setup</h2>"
            "<form action='/save' method='POST'>"
            "SSID:<br>"
            "<input name='ssid'><br>"
            "Password:<br>"
            "<input name='password' type='password'><br><br>"
            "<input type='submit' value='Save'>"
            "</form>"
            "</body></html>"
        );
    }


    void handleSave()
    {
        if (!_server.hasArg("ssid"))
        {
            _server.send(
                400,
                "text/plain",
                "Missing SSID"
            );

            return;
        }


        const String ssid =
            _server.arg("ssid");

        const String password =
            _server.arg("password");


        if (
            ssid.length() == 0 ||
            ssid.length() >= sizeof(_ssid)
        )
        {
            _server.send(
                400,
                "text/plain",
                "Invalid SSID"
            );

            return;
        }


        if (
            password.length() >= sizeof(_password)
        )
        {
            _server.send(
                400,
                "text/plain",
                "Invalid password"
            );

            return;
        }


        ssid.toCharArray(
            _ssid,
            sizeof(_ssid)
        );

        password.toCharArray(
            _password,
            sizeof(_password)
        );


        saveCredentials();


        _server.send(
            200,
            "text/html",
            "<html><body>"
            "<h2>Saved</h2>"
            "<p>Connecting to WiFi...</p>"
            "</body></html>"
        );


        /*
         * Do not immediately manipulate WiFi here.
         *
         * handleProvisioning() will see this flag after
         * handleClient() returns and transition to STA mode.
         */
        _provisioningComplete = true;
    }


    // =========================================================================
    // Save credentials
    // =========================================================================

    void saveCredentials()
    {
#if defined(ESP32)

        Preferences preferences;

        preferences.begin(
            "wifi",
            false
        );

        preferences.putString(
            "ssid",
            _ssid
        );

        preferences.putString(
            "password",
            _password
        );

        preferences.end();

#elif defined(ESP8266)

        EEPROM.begin(EEPROM_SIZE);


        for (size_t i = 0; i < 33; ++i)
        {
            EEPROM.write(
                i,
                i < strlen(_ssid)
                    ? _ssid[i]
                    : 0
            );
        }


        for (size_t i = 0; i < 65; ++i)
        {
            EEPROM.write(
                33 + i,
                i < strlen(_password)
                    ? _password[i]
                    : 0
            );
        }


        EEPROM.commit();
        EEPROM.end();

#endif
    }


#if defined(ESP32)

    WebServer _server{80};

#elif defined(ESP8266)

    ESP8266WebServer _server{80};

#endif
};