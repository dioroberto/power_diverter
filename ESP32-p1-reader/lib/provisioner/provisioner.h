#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#define AP_SSID "DEVICE PROVISIONING"

class Provisioner {
public:
    Provisioner();

    // Load credentials from NVS.
    // Returns false if no credentials are stored.
    bool get_creds(char *ssid, char *pass);

    // Clear stored credentials.
    void clear_creds();

    // Run provisioning until valid WiFi credentials are supplied.
    // Credentials are only saved after a successful connection.
    bool provision(char *ssid, char *pass);

private:
    WebServer _web_server{80};
    Preferences _preferences;

    bool _provisioning_complete = false;
    bool _connection_successful = false;

    char _ssid[32] = {};
    char _pass[64] = {};

    void _start_provisioner();
    void _wait_for_completion();

    bool _load_creds();
    void _save_creds();

    bool _connect_wifi();
};
    
extern Provisioner provisioner;