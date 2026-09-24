#include "MQTTClient.h"
#include "settings.h"


MQTTClient* MQTTClient::_instance = nullptr;


// =============================================================================
// Constructor
// =============================================================================

MQTTClient::MQTTClient(
    const char* host,
    uint16_t port,
    const char* path,
    const char* protocol,
    const char* username,
    const char* password,
    const char* deviceName
)
    : _host(host),
      _port(port),
      _path(path),
      _protocol(protocol),
      _username(username),
      _password(password),
      _deviceName(deviceName),
      _p1DataTopic(Settings::MQTT::Topic::DATA()),
      _subscribedTopic(),
      _messageCallback(nullptr),
      _state(State::STOPPED),
      _started(false),
      _mqttSessionValid(false),
      _nextMqttConnect(0),
      _mqttReconnectInterval(
          Settings::MQTT::RECONNECT_INTERVAL_MS
      )
{
    _instance = this;
}


// =============================================================================
// begin
// =============================================================================

void MQTTClient::begin()
{
    if (_started)
        return;


    _started = true;
    _state = State::WAITING_FOR_WEBSOCKET;
    _mqttSessionValid = false;
    _nextMqttConnect = 0;


    Serial.println();
    Serial.println("==================================");
    Serial.println(" MQTT / WSS");
    Serial.println("==================================");

    Serial.printf(
        "Host: %s\n",
        _host
    );

    Serial.printf(
        "Port: %u\n",
        _port
    );

    Serial.printf(
        "Path: %s\n",
        _path
    );

    Serial.printf(
        "WebSocket protocol: %s\n",
        _protocol
    );

    Serial.printf(
        "Username: %s\n",
        _username
    );

    Serial.printf(
        "Device name: %s\n",
        _deviceName
    );

    Serial.printf(
        "Client ID: %s\n",
        _deviceName
    );

    Serial.printf(
        "P1 data topic: %s\n",
        _p1DataTopic.c_str()
    );

    Serial.printf(
        "Availability topic: %s\n",
        Settings::MQTT::Topic::AVAILABILITY().c_str()
    );

    Serial.println();
    Serial.println("Starting WSS...");


    // -------------------------------------------------------------------------
    // WebSocket Secure
    //
    // This is initialized exactly once.
    // The WebSocket library is responsible for reconnecting the transport.
    // -------------------------------------------------------------------------

    _webSocket.beginSSL(
        _host,
        _port,
        _path,
        nullptr,
        _protocol
    );

    _webSocket.onEvent(
        MQTTClient::webSocketEvent
    );

    _webSocket.setReconnectInterval(
        _mqttReconnectInterval
    );


    // -------------------------------------------------------------------------
    // MQTT
    // -------------------------------------------------------------------------

    _mqtt.begin(_webSocket);

    _mqtt.setOptions(
        60,       // keepalive seconds
        true,     // clean session
        10000     // timeout ms
    );


    Serial.println("MQTT client started");
    Serial.println("Waiting for WebSocket connection...");
}


// =============================================================================
// loop
// =============================================================================

void MQTTClient::loop()
{
    if (!_started)
        return;


    // -------------------------------------------------------------------------
    // Always service the WebSocket transport.
    //
    // This must continue running even when Wi-Fi is temporarily unavailable.
    // The WebSocket library handles its own reconnect attempts.
    // -------------------------------------------------------------------------

    _webSocket.loop();


    // -------------------------------------------------------------------------
    // WebSocket transport unavailable.
    //
    // There cannot be a valid MQTT session without the WebSocket transport.
    //
    // IMPORTANT:
    // Do NOT call _mqtt.disconnect() here. The transport may already be gone.
    // -------------------------------------------------------------------------

    if (!_webSocket.isConnected())
    {
        if (_mqttSessionValid)
        {
            invalidateMqttSession();
        }

        if (_state != State::WAITING_FOR_WEBSOCKET)
        {
            _state = State::WAITING_FOR_WEBSOCKET;

            Serial.println();
            Serial.println("Waiting for WebSocket...");
        }

        return;
    }


    // -------------------------------------------------------------------------
    // WebSocket is connected.
    //
    // A newly established WebSocket always requires a new MQTT CONNECT.
    // -------------------------------------------------------------------------

    if (!_mqttSessionValid)
    {
        if (_state != State::CONNECTING_MQTT)
        {
            _state = State::CONNECTING_MQTT;

            Serial.println();
            Serial.println("WebSocket connected");
            Serial.println("MQTT transport ready");
            Serial.println("MQTT session requires CONNECT");
        }


        if (!reconnectDue())
            return;


        connectMqtt();

        return;
    }


    // -------------------------------------------------------------------------
    // Current MQTT session is valid.
    //
    // Process MQTT traffic only while both layers agree that the connection
    // is active.
    // -------------------------------------------------------------------------

    if (_mqtt.isConnected())
    {
        _mqtt.update();


        if (_state != State::CONNECTED)
        {
            _state = State::CONNECTED;

            Serial.println();
            Serial.println("==================================");
            Serial.println(" MQTT CONNECTION ACTIVE");
            Serial.println("==================================");
        }

        return;
    }


    // -------------------------------------------------------------------------
    // Defensive recovery.
    //
    // The MQTT library no longer considers the session connected although
    // the WebSocket transport is still alive.
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println("[MQTT] Session lost");

    invalidateMqttSession();

    _state = State::CONNECTING_MQTT;

    scheduleMqttReconnect();
}


// =============================================================================
// connected
// =============================================================================

bool MQTTClient::connected()
{
    return
        _started &&
        _webSocket.isConnected() &&
        _mqttSessionValid &&
        _mqtt.isConnected();
}


// =============================================================================
// state
// =============================================================================

MQTTClient::State MQTTClient::state() const
{
    return _state;
}


// =============================================================================
// connect
// =============================================================================

bool MQTTClient::connect()
{
    if (!_started)
        begin();


    // -------------------------------------------------------------------------
    // Already connected.
    // -------------------------------------------------------------------------

    if (connected())
    {
        _state = State::CONNECTED;
        return true;
    }


    // -------------------------------------------------------------------------
    // WebSocket transport must exist first.
    // -------------------------------------------------------------------------

    if (!_webSocket.isConnected())
    {
        Serial.println();
        Serial.println("MQTT connect requested");
        Serial.println("WebSocket not connected yet");

        _state = State::WAITING_FOR_WEBSOCKET;

        return false;
    }


    // -------------------------------------------------------------------------
    // Force a completely new MQTT session.
    // -------------------------------------------------------------------------

    invalidateMqttSession();

    return connectMqtt();
}


// =============================================================================
// configureLastWill
// =============================================================================

void MQTTClient::configureLastWill()
{
    const String availabilityTopic =
        Settings::MQTT::Topic::AVAILABILITY();


    // -------------------------------------------------------------------------
    // Configure Last Will before every MQTT CONNECT.
    //
    // Unexpected loss:
    //
    //     <device>/availability = offline
    //
    // Normal DISCONNECT does not trigger the Last Will.
    // -------------------------------------------------------------------------

    _mqtt.setWill(
        availabilityTopic,
        "offline",
        true,
        0
    );
}


// =============================================================================
// connectMqtt
// =============================================================================

bool MQTTClient::connectMqtt()
{
    if (!_started)
        return false;


    // -------------------------------------------------------------------------
    // WebSocket transport must be connected.
    // -------------------------------------------------------------------------

    if (!_webSocket.isConnected())
    {
        _state = State::WAITING_FOR_WEBSOCKET;
        return false;
    }


    // -------------------------------------------------------------------------
    // Already connected.
    // -------------------------------------------------------------------------

    if (_mqttSessionValid && _mqtt.isConnected())
    {
        _state = State::CONNECTED;
        return true;
    }


    // -------------------------------------------------------------------------
    // Clear stale MQTT library state.
    //
    // This is only done while the WebSocket transport is still alive.
    // -------------------------------------------------------------------------

    if (_mqtt.isConnected())
    {
        Serial.println(
            "[MQTT] Clearing stale MQTT session before CONNECT..."
        );

        _mqtt.disconnect();
    }

    _mqttSessionValid = false;


    // -------------------------------------------------------------------------
    // MQTT CONNECT
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println("==================================");
    Serial.println(" MQTT CONNECT");
    Serial.println("==================================");

    Serial.printf(
        "Client ID: %s\n",
        _deviceName
    );


    // -------------------------------------------------------------------------
    // Last Will must be configured before CONNECT.
    // -------------------------------------------------------------------------

    configureLastWill();


    // -------------------------------------------------------------------------
    // Device name is the MQTT client ID.
    // -------------------------------------------------------------------------

    const bool result =
        _mqtt.connect(
            _deviceName,
            _username,
            _password
        );


    // -------------------------------------------------------------------------
    // CONNECT successful.
    // -------------------------------------------------------------------------

    if (result)
    {
        _mqttSessionValid = true;
        _state = State::CONNECTED;
        _nextMqttConnect = 0;


        Serial.println();
        Serial.println("==================================");
        Serial.println(" MQTT CONNECTION SUCCESS");
        Serial.println("==================================");

        Serial.printf(
            "Client ID: %s\n",
            _deviceName
        );

        Serial.printf(
            "Broker: %s:%u\n",
            _host,
            _port
        );


        // ---------------------------------------------------------------------
        // Clean MQTT sessions do not retain subscriptions.
        // ---------------------------------------------------------------------

        restoreSubscriptions();


        // ---------------------------------------------------------------------
        // Announce online.
        // ---------------------------------------------------------------------

        if (publishAvailability(true))
        {
            Serial.println(
                "MQTT availability: online"
            );
        }
        else
        {
            Serial.println(
                "MQTT availability publish FAILED"
            );
        }


        return true;
    }


    // -------------------------------------------------------------------------
    // CONNECT failed.
    // -------------------------------------------------------------------------

    _mqttSessionValid = false;

    Serial.println();
    Serial.println("==================================");
    Serial.println(" MQTT CONNECTION FAILED");
    Serial.println("==================================");

    Serial.printf(
        "Return code: %d\n",
        static_cast<int>(_mqtt.getReturnCode())
    );

    Serial.printf(
        "Error code: %d\n",
        static_cast<int>(_mqtt.getLastError())
    );


    _state = State::CONNECTING_MQTT;

    scheduleMqttReconnect();

    return false;
}


// =============================================================================
// invalidateMqttSession
// =============================================================================

void MQTTClient::invalidateMqttSession()
{
    if (_mqttSessionValid)
    {
        Serial.println(
            "[MQTT] Invalidating MQTT session"
        );
    }


    // -------------------------------------------------------------------------
    // Do not send MQTT DISCONNECT here.
    //
    // This function is also called after WebSocket loss, where the transport
    // may already be gone.
    // -------------------------------------------------------------------------

    _mqttSessionValid = false;
}


// =============================================================================
// mqttSessionValid
// =============================================================================

bool MQTTClient::mqttSessionValid() const
{
    return
        _mqttSessionValid &&
        _mqtt.isConnected();
}


// =============================================================================
// reconnectDue
// =============================================================================

bool MQTTClient::reconnectDue() const
{
    if (_nextMqttConnect == 0)
        return true;


    const unsigned long now = millis();


    return static_cast<long>(
        now - _nextMqttConnect
    ) >= 0;
}


// =============================================================================
// scheduleMqttReconnect
// =============================================================================

void MQTTClient::scheduleMqttReconnect()
{
    _nextMqttConnect =
        millis() + _mqttReconnectInterval;
}


// =============================================================================
// disconnect
// =============================================================================

void MQTTClient::disconnect()
{
    if (!_started)
        return;


    Serial.println();
    Serial.println("MQTT disconnect");


    // -------------------------------------------------------------------------
    // This is an intentional shutdown.
    //
    // Unlike invalidateMqttSession(), the transport is expected to be alive,
    // so sending MQTT DISCONNECT is appropriate.
    // -------------------------------------------------------------------------

    if (_webSocket.isConnected() && _mqtt.isConnected())
    {
        _mqtt.disconnect();
    }


    _mqttSessionValid = false;


    // -------------------------------------------------------------------------
    // Close WebSocket transport.
    // -------------------------------------------------------------------------

    _webSocket.disconnect();


    // -------------------------------------------------------------------------
    // Reset client state.
    // -------------------------------------------------------------------------

    _state = State::STOPPED;
    _started = false;
    _nextMqttConnect = 0;
}


// =============================================================================
// publish
// =============================================================================

bool MQTTClient::publish(
    const char* topic,
    const char* payload,
    bool retained
)
{
    if (topic == nullptr)
        return false;

    if (payload == nullptr)
        return false;


    // -------------------------------------------------------------------------
    // Never publish unless both transport and MQTT session are valid.
    // -------------------------------------------------------------------------

    if (!connected())
        return false;


    return _mqtt.publish(
        String(topic),
        String(payload),
        retained
    );
}


// =============================================================================
// publishP1
// =============================================================================

bool MQTTClient::publishP1(
    const char* telegram,
    bool retained
)
{
    if (telegram == nullptr)
        return false;


    return publish(
        _p1DataTopic.c_str(),
        telegram,
        retained
    );
}


// =============================================================================
// publishP1Health
// =============================================================================

bool MQTTClient::publishP1Health(
    bool available,
    uint32_t telegramAgeMs,
    size_t valueCount,
    uint32_t lastTelegramMs
)
{
    String payload;

    payload.reserve(128);


    payload += F("{\"available\":");
    payload += available ? F("true") : F("false");

    payload += F(",\"telegram_age_ms\":");
    payload += telegramAgeMs;

    payload += F(",\"value_count\":");
    payload += valueCount;

    payload += F(",\"last_telegram_ms\":");
    payload += lastTelegramMs;

    payload += F("}");


    const String topic =
        Settings::MQTT::Topic::P1_STATUS();


    return publish(
        topic.c_str(),
        payload.c_str(),
        true
    );
}


// =============================================================================
// publishAvailability
// =============================================================================

bool MQTTClient::publishAvailability(
    bool available
)
{
    const String topic =
        Settings::MQTT::Topic::AVAILABILITY();


    const char* payload =
        available
            ? "online"
            : "offline";


    return publish(
        topic.c_str(),
        payload,
        true
    );
}


// =============================================================================
// subscribe
// =============================================================================

bool MQTTClient::subscribe(
    const char* topic
)
{
    if (topic == nullptr)
        return false;

    if (topic[0] == '\0')
        return false;


    // -------------------------------------------------------------------------
    // Always remember the subscription.
    //
    // This allows restoreSubscriptions() to restore it after every clean
    // MQTT session.
    // -------------------------------------------------------------------------

    _subscribedTopic = topic;


    // -------------------------------------------------------------------------
    // MQTT isn't connected yet.
    //
    // The subscription remains queued.
    // -------------------------------------------------------------------------

    if (!connected())
    {
        Serial.println();

        Serial.printf(
            "MQTT subscription queued: %s\n",
            _subscribedTopic.c_str()
        );

        return true;
    }


    // -------------------------------------------------------------------------
    // Subscribe immediately.
    // -------------------------------------------------------------------------

    const bool result =
        _mqtt.subscribe(
            _subscribedTopic,
            MQTTClient::mqttMessageCallback
        );


    if (result)
    {
        Serial.println();

        Serial.printf(
            "MQTT subscribed: %s\n",
            _subscribedTopic.c_str()
        );
    }
    else
    {
        Serial.println();

        Serial.printf(
            "MQTT subscribe FAILED: %s\n",
            _subscribedTopic.c_str()
        );
    }


    return result;
}


// =============================================================================
// subscribeP1
// =============================================================================

bool MQTTClient::subscribeP1()
{
    return subscribe(
        _p1DataTopic.c_str()
    );
}


// =============================================================================
// p1DataTopic
// =============================================================================

const char* MQTTClient::p1DataTopic() const
{
    return _p1DataTopic.c_str();
}


// =============================================================================
// restoreSubscriptions
// =============================================================================

void MQTTClient::restoreSubscriptions()
{
    if (!connected())
        return;

    if (_subscribedTopic.length() == 0)
        return;


    Serial.println();
    Serial.println("Restoring MQTT subscriptions...");


    const bool result =
        _mqtt.subscribe(
            _subscribedTopic,
            MQTTClient::mqttMessageCallback
        );


    if (result)
    {
        Serial.printf(
            "Subscription restored: %s\n",
            _subscribedTopic.c_str()
        );
    }
    else
    {
        Serial.printf(
            "Subscription restore FAILED: %s\n",
            _subscribedTopic.c_str()
        );
    }
}


// =============================================================================
// setMessageCallback
// =============================================================================

void MQTTClient::setMessageCallback(
    MessageCallback callback
)
{
    _messageCallback = callback;
}


// =============================================================================
// WebSocket event callback
// =============================================================================

void MQTTClient::webSocketEvent(
    WStype_t type,
    uint8_t* payload,
    size_t length
)
{
    if (_instance == nullptr)
        return;


    switch (type)
    {
        // ---------------------------------------------------------------------
        // WebSocket disconnected
        // ---------------------------------------------------------------------

        case WStype_DISCONNECTED:

            Serial.println();
            Serial.println("[WS] DISCONNECTED");


            // -----------------------------------------------------------------
            // The MQTT session is no longer valid.
            //
            // Do NOT call _mqtt.disconnect().
            // The WebSocket may already be gone.
            // -----------------------------------------------------------------

            _instance->invalidateMqttSession();

            _instance->_state =
                State::WAITING_FOR_WEBSOCKET;

            // Wait for the WebSocket layer to reconnect.
            _instance->_nextMqttConnect =
                0;

            break;


        // ---------------------------------------------------------------------
        // WebSocket connected
        // ---------------------------------------------------------------------

        case WStype_CONNECTED:

            Serial.println();
            Serial.println("[WS] CONNECTED");
            Serial.println("WebSocket connection SUCCESS");


            // -----------------------------------------------------------------
            // Every WebSocket connection requires a new MQTT CONNECT.
            // -----------------------------------------------------------------

            _instance->invalidateMqttSession();

            _instance->_state =
                State::CONNECTING_MQTT;

            // MQTT CONNECT can happen on the next loop iteration.
            _instance->_nextMqttConnect =
                0;

            break;


        // ---------------------------------------------------------------------
        // WebSocket error
        // ---------------------------------------------------------------------

        case WStype_ERROR:

            Serial.println();
            Serial.println("[WS] ERROR");


            _instance->invalidateMqttSession();

            _instance->_state =
                State::WAITING_FOR_WEBSOCKET;

            _instance->_nextMqttConnect =
                0;

            break;


        // ---------------------------------------------------------------------
        // Text frame
        // ---------------------------------------------------------------------

        case WStype_TEXT:

            Serial.printf(
                "[WS] TEXT: %.*s\n",
                static_cast<int>(length),
                reinterpret_cast<const char*>(payload)
            );

            break;


        // ---------------------------------------------------------------------
        // Binary frame
        // ---------------------------------------------------------------------

        case WStype_BIN:

            Serial.printf(
                "[WS] BINARY: %u bytes\n",
                static_cast<unsigned int>(length)
            );

            break;


        // ---------------------------------------------------------------------
        // Ping
        // ---------------------------------------------------------------------

        case WStype_PING:

            Serial.println("[WS] PING");

            break;


        // ---------------------------------------------------------------------
        // Pong
        // ---------------------------------------------------------------------

        case WStype_PONG:

            Serial.println("[WS] PONG");

            break;


        default:
            break;
    }
}


// =============================================================================
// MQTT message callback
// =============================================================================

void MQTTClient::mqttMessageCallback(
    const char* payload,
    unsigned int size
)
{
    if (_instance == nullptr)
        return;

    if (_instance->_messageCallback == nullptr)
        return;


    _instance->_messageCallback(
        _instance->_subscribedTopic.c_str(),
        payload,
        size
    );
}