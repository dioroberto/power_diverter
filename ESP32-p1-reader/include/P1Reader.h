#pragma once

#include <Arduino.h>


struct P1Value
{
    String obis;
    String value;
};


class P1Reader
{
public:

    P1Reader(
        HardwareSerial& serial,
        int rxPin,
        int txPin,
        uint32_t baudrate,
        uint32_t telegramTimeoutMs
    );

    void begin();
    void loop();

    bool available();


    // =========================================================
    // Parsed OBIS values
    // =========================================================

    size_t getValueCount() const;

    const P1Value& getValue(size_t index) const;


    // =========================================================
    // Last successfully received telegram
    // =========================================================

    const String& getLastTelegram() const;


    // =========================================================
    // Telegram statistics
    // =========================================================

    uint32_t getValidTelegramCount() const;
    uint32_t getInvalidTelegramCount() const;

    uint32_t getRxByteCount() const;
    uint32_t getTelegramStartCount() const;
    uint32_t getTelegramEndCount() const;
    uint32_t getCrcErrorCount() const;
    uint32_t getBufferOverflowCount() const;
    uint32_t getTelegramTimeoutCount() const;

    uint16_t getLastReceivedCrc() const;
    uint16_t getLastCalculatedCrc() const;
    uint32_t getLastTelegramLength() const;

    const String& getLastInvalidTelegram() const;

    bool isReceiving() const;


    // =========================================================
    // Power
    // =========================================================

    float importedPower() const;
    float exportedPower() const;
    float netPower() const;


private:

    // =========================================================
    // Serial configuration
    // =========================================================

    HardwareSerial& serial_;

    int rxPin_;
    int txPin_;

    uint32_t baudrate_;
    uint32_t telegramTimeoutMs_;


    // =========================================================
    // Current telegram reception
    // =========================================================

    String telegram_;

    bool receiving_ = false;

    uint32_t lastCharacterTime_ = 0;


    // =========================================================
    // Last successfully completed telegram
    // =========================================================

    String rawTelegram_;

    String lastInvalidTelegram_;


    // =========================================================
    // Parsed values
    // =========================================================

    static constexpr size_t MAX_VALUES = 50;

    P1Value values_[MAX_VALUES];

    size_t valueCount_ = 0;


    // =========================================================
    // Telegram status
    // =========================================================

    bool dataAvailable_ = false;


    // =========================================================
    // Statistics
    // =========================================================

    uint32_t validTelegramCount_ = 0;
    uint32_t invalidTelegramCount_ = 0;

    uint32_t rxByteCount_ = 0;
    uint32_t telegramStartCount_ = 0;
    uint32_t telegramEndCount_ = 0;
    uint32_t crcErrorCount_ = 0;
    uint32_t bufferOverflowCount_ = 0;
    uint32_t telegramTimeoutCount_ = 0;

    uint16_t lastReceivedCrc_ = 0;
    uint16_t lastCalculatedCrc_ = 0;
    uint32_t lastTelegramLength_ = 0;


    // =========================================================
    // Internal processing
    // =========================================================

    void processCharacter(char c);

    void processTelegram();

    void resetTelegram();


    // =========================================================
    // CRC
    // =========================================================

    bool validateCRC(const String& telegram);

    uint16_t calculateCRC(
        const uint8_t* data,
        size_t length
    );


    // =========================================================
    // Telegram parsing
    // =========================================================

    void parseTelegram(const String& telegram);
};