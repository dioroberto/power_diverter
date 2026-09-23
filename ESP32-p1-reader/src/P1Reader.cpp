#include "P1Reader.h"

#include "settings.h"
#include "driver/uart.h"

#include <cstdlib>


namespace
{
    constexpr const char* OBIS_IMPORTED_POWER =
        "1-0:1.7.0";

    constexpr const char* OBIS_EXPORTED_POWER =
        "1-0:2.7.0";
}


// ============================================================
// Constructor
// ============================================================

P1Reader::P1Reader(
    HardwareSerial& serial,
    int rxPin,
    int txPin,
    uint32_t baudrate,
    uint32_t telegramTimeoutMs
)
    : serial_(serial),
      rxPin_(rxPin),
      txPin_(txPin),
      baudrate_(baudrate),
      telegramTimeoutMs_(telegramTimeoutMs)
{
}


// ============================================================
// Begin
// ============================================================

void P1Reader::begin()
{
    serial_.begin(
        baudrate_,
        SERIAL_8N1,
        rxPin_,
        txPin_
    );

    // --------------------------------------------------------
    // P1 uses inverted TTL logic.
    // Invert RX only.
    // --------------------------------------------------------

    if (Settings::P1::RX_INVERTED)
    {
        uart_set_line_inverse(
            UART_NUM_2,
            UART_SIGNAL_RXD_INV
        );
    }

    lastInvalidTelegram_.clear();

    telegram_.clear();
    rawTelegram_.clear();

    receiving_ = false;
    lastCharacterTime_ = 0;

    valueCount_ = 0;
    dataAvailable_ = false;

    validTelegramCount_ = 0;
    invalidTelegramCount_ = 0;

    rxByteCount_ = 0;
    telegramStartCount_ = 0;
    telegramEndCount_ = 0;
    crcErrorCount_ = 0;
    bufferOverflowCount_ = 0;
    telegramTimeoutCount_ = 0;

    Serial.println("P1 reader started");

    Serial.printf(
        "P1 UART: %lu baud, RX=%d, TX=%d, RX inverted=%s\n",
        static_cast<unsigned long>(baudrate_),
        rxPin_,
        txPin_,
        Settings::P1::RX_INVERTED ? "YES" : "NO"
    );
}


// ============================================================
// Loop
// ============================================================

void P1Reader::loop()
{
    // --------------------------------------------------------
    // Abort incomplete telegram after timeout.
    // --------------------------------------------------------

    if (
        receiving_ &&
        millis() - lastCharacterTime_ >
            telegramTimeoutMs_
    )
    {
        ++telegramTimeoutCount_;

        if (Settings::P1::DEBUG_LEVEL >= 1)
        {
            Serial.printf(
                "P1: TELEGRAM TIMEOUT after %lu ms\n",
                static_cast<unsigned long>(
                    telegramTimeoutMs_
                )
            );
        }

        resetTelegram();
    }


    // --------------------------------------------------------
    // Read every byte currently available.
    // --------------------------------------------------------

    while (serial_.available() > 0)
    {
        const char c =
            static_cast<char>(serial_.read());

        ++rxByteCount_;

        lastCharacterTime_ = millis();

        processCharacter(c);
    }
}


// ============================================================
// Available
// ============================================================

bool P1Reader::available()
{
    return dataAvailable_;
}


// ============================================================
// Parsed values
// ============================================================

size_t P1Reader::getValueCount() const
{
    return valueCount_;
}


const P1Value& P1Reader::getValue(size_t index) const
{
    static const P1Value emptyValue = {
        String(),
        String()
    };

    if (index >= valueCount_)
    {
        return emptyValue;
    }

    return values_[index];
}


// ============================================================
// Last raw telegram
// ============================================================

const String& P1Reader::getLastTelegram() const
{
    return rawTelegram_;
}


// ============================================================
// Statistics
// ============================================================

uint32_t P1Reader::getValidTelegramCount() const
{
    return validTelegramCount_;
}


uint32_t P1Reader::getInvalidTelegramCount() const
{
    return invalidTelegramCount_;
}


uint32_t P1Reader::getRxByteCount() const
{
    return rxByteCount_;
}


uint32_t P1Reader::getTelegramStartCount() const
{
    return telegramStartCount_;
}


uint32_t P1Reader::getTelegramEndCount() const
{
    return telegramEndCount_;
}


uint32_t P1Reader::getCrcErrorCount() const
{
    return crcErrorCount_;
}


uint32_t P1Reader::getBufferOverflowCount() const
{
    return bufferOverflowCount_;
}


uint32_t P1Reader::getTelegramTimeoutCount() const
{
    return telegramTimeoutCount_;
}


bool P1Reader::isReceiving() const
{
    return receiving_;
}

uint16_t P1Reader::getLastReceivedCrc() const
{
    return lastReceivedCrc_;
}


uint16_t P1Reader::getLastCalculatedCrc() const
{
    return lastCalculatedCrc_;
}


uint32_t P1Reader::getLastTelegramLength() const
{
    return lastTelegramLength_;
}

// ============================================================
// Process character
// ============================================================

void P1Reader::processCharacter(char c)
{
    if (Settings::P1::DEBUG_LEVEL >= 3)
    {
        const uint8_t byte =
            static_cast<uint8_t>(c);

        Serial.printf(
            "P1 RX: 0x%02X '%c'\n",
            byte,
            (byte >= 32 && byte <= 126)
                ? byte
                : '.'
        );
    }


    // --------------------------------------------------------
    // Start of telegram.
    //
    // DSMR telegram starts with '/'.
    // --------------------------------------------------------

    if (c == '/')
    {
        ++telegramStartCount_;

        if (Settings::P1::DEBUG_LEVEL >= 1)
        {
            Serial.println(
                "P1: START '/' detected"
            );
        }

        // A new telegram supersedes previous notification.
        dataAvailable_ = false;

        resetTelegram();

        receiving_ = true;

        telegram_ += c;

        lastCharacterTime_ = millis();

        return;
    }


    // --------------------------------------------------------
    // Ignore bytes outside a telegram.
    // --------------------------------------------------------

    if (!receiving_)
    {
        return;
    }


    // --------------------------------------------------------
    // Protect buffer.
    // --------------------------------------------------------

    if (
        telegram_.length() >=
        Settings::P1::BUFFER_SIZE - 1
    )
    {
        ++bufferOverflowCount_;
        ++invalidTelegramCount_;

        if (Settings::P1::DEBUG_LEVEL >= 1)
        {
            Serial.printf(
                "P1: BUFFER OVERFLOW at %u bytes\n",
                static_cast<unsigned int>(
                    telegram_.length()
                )
            );
        }

        resetTelegram();

        return;
    }


    // --------------------------------------------------------
    // Append byte.
    // --------------------------------------------------------

    telegram_ += c;

    lastCharacterTime_ = millis();


    // --------------------------------------------------------
    // Detect end marker.
    //
    // DSMR:
    //
    //     !XXXX
    //
    // where XXXX is four hexadecimal CRC characters.
    // --------------------------------------------------------

    const int exclamation =
        telegram_.indexOf('!');

    if (
        exclamation >= 0 &&
        telegram_.length() >=
            static_cast<size_t>(
                exclamation + 5
            )
    )
    {
        ++telegramEndCount_;

        if (Settings::P1::DEBUG_LEVEL >= 1)
        {
            Serial.println(
                "P1: END '!'+CRC detected"
            );
        }

        processTelegram();

        // Reset only the reception state.
        // Keep parsed data and dataAvailable_.
        resetTelegram();
    }
}


// ============================================================
// Process complete telegram
// ============================================================

void P1Reader::processTelegram()
{
    if (Settings::P1::DEBUG_LEVEL >= 2)
    {
        Serial.println();
        Serial.println(
            "========== P1 TELEGRAM =========="
        );

        Serial.print(telegram_);

        Serial.println(
            "================================="
        );
    }


    // --------------------------------------------------------
    // CRC validation.
    // --------------------------------------------------------

    if (!validateCRC(telegram_))
    {
        lastInvalidTelegram_ = telegram_;

        invalidTelegramCount_++;
        crcErrorCount_++;

        return;
    }

    // --------------------------------------------------------
    // Preserve raw telegram.
    // --------------------------------------------------------

    rawTelegram_ = telegram_;


    // --------------------------------------------------------
    // Parse OBIS values.
    // --------------------------------------------------------

    parseTelegram(rawTelegram_);


    // --------------------------------------------------------
    // Valid telegram.
    // --------------------------------------------------------

    ++validTelegramCount_;

    dataAvailable_ = true;

    if (Settings::P1::DEBUG_LEVEL >= 1)
    {
        Serial.printf(
            "P1: CRC OK, %u OBIS values, %u bytes\n",
            static_cast<unsigned int>(valueCount_),
            static_cast<unsigned int>(
                rawTelegram_.length()
            )
        );
    }
}


// ============================================================
// Reset current telegram
// ============================================================

void P1Reader::resetTelegram()
{
    telegram_.clear();

    receiving_ = false;

    lastCharacterTime_ = 0;
}


// ============================================================
// Parse telegram
// ============================================================

void P1Reader::parseTelegram(
    const String& telegram
)
{
    valueCount_ = 0;

    int lineStart = 0;

    while (
        lineStart <
        static_cast<int>(
            telegram.length()
        )
    )
    {
        const int lineEnd =
            telegram.indexOf(
                '\n',
                lineStart
            );

        const int end =
            lineEnd >= 0
                ? lineEnd
                : telegram.length();

        String line =
            telegram.substring(
                lineStart,
                end
            );

        line.trim();


        // ----------------------------------------------------
        // Expected:
        //
        //     OBIS(value)
        //
        // Units/scalers after ')' are ignored.
        // ----------------------------------------------------

        if (
            line.length() > 0 &&
            line.indexOf('(') > 0
        )
        {
            const int open =
                line.indexOf('(');

            String obis =
                line.substring(
                    0,
                    open
                );

            obis.trim();


            if (
                obis.length() > 0 &&
                valueCount_ < MAX_VALUES
            )
            {
                const int close =
                    line.indexOf(
                        ')',
                        open + 1
                    );

                if (close > open)
                {
                    values_[valueCount_].obis =
                        obis;

                    values_[valueCount_].value =
                        line.substring(
                            open + 1,
                            close
                        );

                    values_[valueCount_].value.trim();

                    ++valueCount_;
                }
            }
        }


        if (lineEnd < 0)
        {
            break;
        }

        lineStart = lineEnd + 1;
    }
}


// ============================================================
// Imported power
// ============================================================

float P1Reader::importedPower() const
{
    for (size_t i = 0; i < valueCount_; ++i)
    {
        const P1Value& value = values_[i];

        if (value.obis == OBIS_IMPORTED_POWER)
        {
            return value.value.toFloat();
        }
    }

    return 0.0f;
}


// ============================================================
// Exported power
// ============================================================

float P1Reader::exportedPower() const
{
    for (size_t i = 0; i < valueCount_; ++i)
    {
        const P1Value& value = values_[i];

        if (value.obis == OBIS_EXPORTED_POWER)
        {
            return value.value.toFloat();
        }
    }

    return 0.0f;
}


// ============================================================
// Net power
// ============================================================

float P1Reader::netPower() const
{
    return importedPower() - exportedPower();
}

const String& P1Reader::getLastInvalidTelegram() const
    {
        return lastInvalidTelegram_;
    }

// ============================================================
// CRC validation
// ============================================================

bool P1Reader::validateCRC(
    const String& telegram
)
{
    const int exclamation =
        telegram.indexOf('!');

    if (exclamation < 0)
    {
        return false;
    }

    if (
        telegram.length() <
        static_cast<size_t>(exclamation + 5)
    )
    {
        return false;
    }

    const String crcString =
        telegram.substring(
            exclamation + 1,
            exclamation + 5
        );

    for (size_t i = 0; i < 4; ++i)
    {
        const char c = crcString[i];

        const bool hex =
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');

        if (!hex)
        {
            return false;
        }
    }

    lastReceivedCrc_ =
        static_cast<uint16_t>(
            strtoul(
                crcString.c_str(),
                nullptr,
                16
            )
        );

    lastCalculatedCrc_ =
        calculateCRC(
            reinterpret_cast<const uint8_t*>(
                telegram.c_str()
            ),
            static_cast<size_t>(
                exclamation + 1
            )
        );

    lastTelegramLength_ =
        static_cast<uint32_t>(
            exclamation + 1
        );

    if (Settings::P1::DEBUG_LEVEL >= 2)
    {
        Serial.printf(
            "P1 CRC: received=%04X calculated=%04X length=%lu\n",
            lastReceivedCrc_,
            lastCalculatedCrc_,
            static_cast<unsigned long>(
                lastTelegramLength_
            )
        );
    }

    return
        lastReceivedCrc_ ==
        lastCalculatedCrc_;
}

// ============================================================
// CRC-16/CCITT
// ============================================================

uint16_t P1Reader::calculateCRC(
    const uint8_t* data,
    size_t length
)
{
    uint16_t crc = 0x0000;

    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            if (crc & 0x0001)
            {
                crc =
                    static_cast<uint16_t>(
                        (crc >> 1) ^ 0xA001
                    );
            }
            else
            {
                crc =
                    static_cast<uint16_t>(
                        crc >> 1
                    );
            }
        }
    }

    return crc;
}