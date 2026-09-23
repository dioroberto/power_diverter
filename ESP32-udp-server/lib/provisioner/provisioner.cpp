#include "provisioner.h"
#include "html.h"

namespace {
    constexpr const char *NVS_NAMESPACE = "wifi";
    constexpr const char *NVS_SSID      = "ssid";
    constexpr const char *NVS_PASSWORD  = "password";

    constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
}

Provisioner::Provisioner()
{
}

Provisioner provisioner;


// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Provisioner::get_creds(char *ssid, char *pass)
{
    if (!_load_creds()) {
        return false;
    }

    strcpy(ssid, _ssid);
    strcpy(pass, _pass);

    return true;
}


void Provisioner::clear_creds()
{
    _preferences.begin(NVS_NAMESPACE, false);
    _preferences.clear();
    _preferences.end();

    _ssid[0] = '\0';
    _pass[0] = '\0';

    Serial.println("WiFi credentials cleared.");
}


bool Provisioner::provision(char *ssid, char *pass)
{
    _provisioning_complete = false;
    _connection_successful = false;

    _start_provisioner();
    _wait_for_completion();

    if (!_connection_successful) {
        return false;
    }

    strcpy(ssid, _ssid);
    strcpy(pass, _pass);

    return true;
}


// -----------------------------------------------------------------------------
// Persistent credentials
// -----------------------------------------------------------------------------

bool Provisioner::_load_creds()
{
    _preferences.begin(NVS_NAMESPACE, true);

    String ssid = _preferences.getString(NVS_SSID, "");
    String pass = _preferences.getString(NVS_PASSWORD, "");

    _preferences.end();

    if (ssid.isEmpty()) {
        return false;
    }

    strncpy(_ssid, ssid.c_str(), sizeof(_ssid) - 1);
    _ssid[sizeof(_ssid) - 1] = '\0';

    strncpy(_pass, pass.c_str(), sizeof(_pass) - 1);
    _pass[sizeof(_pass) - 1] = '\0';

    return true;
}


void Provisioner::_save_creds()
{
    _preferences.begin(NVS_NAMESPACE, false);

    _preferences.putString(NVS_SSID, _ssid);
    _preferences.putString(NVS_PASSWORD, _pass);

    _preferences.end();

    Serial.println("WiFi credentials saved.");
}


// -----------------------------------------------------------------------------
// Provisioning server
// -----------------------------------------------------------------------------

void Provisioner::_start_provisioner()
{
    Serial.println();
    Serial.println("  >> entering provisioning mode...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);

    Serial.print("  >> provisioning AP: ");
    Serial.println(AP_SSID);

    Serial.print("  >> provisioning IP: ");
    Serial.println(WiFi.softAPIP());

    _web_server.on("/", [this]() {
        Serial.println("\t.. base route hit");

        _web_server.send(
            200,
            "text/html",
            CONFIG_PAGE
        );
    });


    _web_server.on("/save", [this]() {

        Serial.println("\t.. save route hit");

        String ssid = _web_server.arg("ssid");
        String pass = _web_server.arg("pass");

        // Validate SSID length.
        if (ssid.isEmpty() || ssid.length() >= sizeof(_ssid)) {

            _web_server.send(
                400,
                "text/plain",
                "Invalid SSID."
            );

            return;
        }

        // Validate password length.
        if (pass.length() >= sizeof(_pass)) {

            _web_server.send(
                400,
                "text/plain",
                "Password is too long."
            );

            return;
        }

        strncpy(_ssid, ssid.c_str(), sizeof(_ssid) - 1);
        _ssid[sizeof(_ssid) - 1] = '\0';

        strncpy(_pass, pass.c_str(), sizeof(_pass) - 1);
        _pass[sizeof(_pass) - 1] = '\0';

        Serial.println();
        Serial.print("\tnew SSID: ");
        Serial.println(_ssid);

        Serial.print("\tnew PASS: ");
        Serial.println(_pass);

        // ---------------------------------------------------------------------
        // Don't save yet.
        //
        // First verify that the credentials actually work.
        // ---------------------------------------------------------------------

        _web_server.send(
            200,
            "text/html",
            SAVED_PAGE
        );

        _connection_successful = _connect_wifi();

        if (_connection_successful) {

            _save_creds();

            _provisioning_complete = true;

            Serial.println();
            Serial.println("  >> provisioning successful.");

        } else {

            Serial.println();
            Serial.println("  >> credentials failed.");
            Serial.println("  >> returning to provisioning mode.");

            // Return to AP mode so the user can try again.
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID);
        }
    });


    _web_server.on("/favicon.ico", [this]() {
        _web_server.send(204);
    });


    _web_server.begin();

    Serial.println("  >> provisioning server started.");
}


// -----------------------------------------------------------------------------
// Wait for successful provisioning
// -----------------------------------------------------------------------------

void Provisioner::_wait_for_completion()
{
    while (!_provisioning_complete) {

        _web_server.handleClient();

        delay(10);
    }

    _web_server.stop();

    WiFi.softAPdisconnect(true);

    Serial.println("  >> exiting provisioning mode...");
}


// -----------------------------------------------------------------------------
// Test WiFi credentials
// -----------------------------------------------------------------------------

bool Provisioner::_connect_wifi()
{
    Serial.println();
    Serial.print("  >> testing WiFi: ");
    Serial.println(_ssid);

    // Stop AP before attempting STA connection.
    WiFi.softAPdisconnect(true);

    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _pass);

    const uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED) {

        if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {

            Serial.println();
            Serial.println("  >> WiFi connection failed.");

            WiFi.disconnect(true);

            return false;
        }

        delay(250);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("  >> WiFi connection successful.");

    Serial.print("  >> IP address: ");
    Serial.println(WiFi.localIP());

    return true;
}