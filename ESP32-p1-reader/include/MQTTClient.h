#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>


class MQTTClient
{
public:

    // =========================================================================
    // Types
    // =========================================================================

    using MessageCallback =
        void (*)(const char* topic,
                 const char* payload,
                 unsigned int size);


    enum class State
    {
        STOPPED,
        WAITING_FOR_WEBSOCKET,
        CONNECTING_MQTT,
        CONNECTED
    };


    // =========================================================================
    // Constructor
    // =========================================================================

    MQTTClient(
        const char* host,
        uint16_t port,
        const char* path,
        const char* protocol,
        const char* username,
        const char* password,
        const char* deviceName
    );


    // =========================================================================
    // Lifecycle
    // =========================================================================

    void begin();
    void loop();
    void disconnect();


    // =========================================================================
    // Connection
    // =========================================================================

    bool connect();
    bool connected();
    State state() const;


    // =========================================================================
    // Generic MQTT
    // =========================================================================

    bool publish(
        const char* topic,
        const char* payload,
        bool retained = false
    );

    bool subscribe(
        const char* topic
    );


    // =========================================================================
    // P1
    // =========================================================================

    bool publishP1(
        const char* telegram,
        bool retained = false
    );

    bool subscribeP1();

    const char* p1DataTopic() const;

    bool publishP1Health(
        bool available,
        uint32_t telegramAgeMs,
        size_t valueCount,
        uint32_t lastTelegramMs
    );


    // =========================================================================
    // Availability
    // =========================================================================

    bool publishAvailability(
        bool available
    );


    // =========================================================================
    // Callback
    // =========================================================================

    void setMessageCallback(
        MessageCallback callback
    );


private:

    // =========================================================================
    // MQTT connection
    // =========================================================================

    bool connectMqtt();

    void configureLastWill();

    void restoreSubscriptions();


    // =========================================================================
    // MQTT session management
    //
    // _mqtt.isConnected() alone is not sufficient when MQTT runs over
    // WebSockets. The WebSocket can disappear while the MQTT object still
    // considers its previous session connected.
    // =========================================================================

    void invalidateMqttSession();

    bool mqttSessionValid() const;


    // =========================================================================
    // Reconnection
    // =========================================================================

    bool reconnectDue() const;

    void scheduleMqttReconnect();


    // =========================================================================
    // WebSocket callback
    // =========================================================================

    static void webSocketEvent(
        WStype_t type,
        uint8_t* payload,
        size_t length
    );


    // =========================================================================
    // MQTT callback
    // =========================================================================

    static void mqttMessageCallback(
        const char* payload,
        unsigned int size
    );


    // =========================================================================
    // WSS configuration
    // =========================================================================

    const char* _host;
    uint16_t    _port;
    const char* _path;
    const char* _protocol;


    // =========================================================================
    // MQTT authentication
    // =========================================================================

    const char* _username;
    const char* _password;


    // =========================================================================
    // Device identity
    //
    // Device name is used directly as the MQTT client ID.
    // No separate client ID is maintained.
    // =========================================================================

    const char* _deviceName;


    // =========================================================================
    // Topics
    // =========================================================================

    String _p1DataTopic;
    String _subscribedTopic;


    // =========================================================================
    // MQTT / WebSocket transport
    // =========================================================================

    WebSocketsClient _webSocket;
    MQTTPubSubClient _mqtt;


    // =========================================================================
    // Callback
    // =========================================================================

    MessageCallback _messageCallback;


    // =========================================================================
    // State
    // =========================================================================

    State _state;
    bool  _started;

    // Explicitly tracks whether the current MQTT session was established
    // after the current WebSocket connection.
    bool  _mqttSessionValid;


    // =========================================================================
    // Reconnection
    // =========================================================================

    unsigned long _nextMqttConnect;
    unsigned long _mqttReconnectInterval;


    // =========================================================================
    // Static callback instance
    // =========================================================================

    static MQTTClient* _instance;
};