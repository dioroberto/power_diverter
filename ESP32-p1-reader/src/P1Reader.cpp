#include "P1Reader.h"

#include "settings.h"
#include "driver/uart.h"

#include <cstdlib>


namespace
{
    // ========================================================
    // Energy
    // ========================================================

    constexpr const char* OBIS_IMPORTED_ENERGY_TARIFF_1 =
        "1-0:1.8.1";

    constexpr const char* OBIS_IMPORTED_ENERGY_TARIFF_2 =
        "1-0:1.8.2";

    constexpr const char* OBIS_EXPORTED_ENERGY_TARIFF_1 =
        "1-0:2.8.1";

    constexpr const char* OBIS_EXPORTED_ENERGY_TARIFF_2 =
        "1-0:2.8.2";


    // ========================================================
    // Power
    // ========================================================

    constexpr const char* OBIS_IMPORTED_POWER =
        "1-0:1.7.0";

    constexpr const char* OBIS_EXPORTED_POWER =
        "1-0:2.7.0";


    // ========================================================
    // Voltage
    // ========================================================

    constexpr const char* OBIS_VOLTAGE_PHASE_1 =
        "1-0:32.7.0";

    constexpr const char* OBIS_VOLTAGE_PHASE_2 =
        "1-0:52.7.0";

    constexpr const char* OBIS_VOLTAGE_PHASE_3 =
        "1-0:72.7.0";


    // ========================================================
    // Current
    // ========================================================

    constexpr const char* OBIS_CURRENT_PHASE_1 =
        "1-0:31.7.0";

    constexpr const char* OBIS_CURRENT_PHASE_2 =
        "1-0:51.7.0";

    constexpr const char* OBIS_CURRENT_PHASE_3 =
        "1-0:71.7.0";
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


    // --------------------------------------------------------
    // Reset runtime state.
    // --------------------------------------------------------

    telegram_.clear();
    rawTelegram_.clear();

    receiving_ = false;
    readingCrc_ = false;
    crcCharactersRead_ = 0;
    lastCharacterTime_ = 0;

    valueCount_ = 0;
    dataAvailable_ = false;


    // --------------------------------------------------------
    // Reset statistics.
    // --------------------------------------------------------

    validTelegramCount_ = 0;
    invalidTelegramCount_ = 0;

    rxByteCount_ = 0;
    telegramStartCount_ = 0;
    telegramEndCount_ = 0;
    crcErrorCount_ = 0;
    bufferOverflowCount_ = 0;
    telegramTimeoutCount_ = 0;

    lastReceivedCrc_ = 0;
    lastCalculatedCrc_ = 0;
    lastTelegramLength_ = 0;


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
    // Abort an incomplete telegram after timeout.
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
    // Read every currently available byte.
    // --------------------------------------------------------

    while (serial_.available() > 0)
    {
        const char c =
            static_cast<char>(serial_.read());

        ++rxByteCount_;

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
// Telegram statistics
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


bool P1Reader::isReceiving() const
{
    return receiving_;
}


// ============================================================
// Generic OBIS float accessor
// ============================================================

float P1Reader::getFloat(const char* obis) const
{
    for (size_t i = 0; i < valueCount_; ++i)
    {
        if (values_[i].obis == obis)
        {
            return values_[i].value.toFloat();
        }
    }

    return 0.0f;
}


// ============================================================
// Energy
// ============================================================

float P1Reader::importedEnergyTariff1() const
{
    return getFloat(OBIS_IMPORTED_ENERGY_TARIFF_1);
}


float P1Reader::importedEnergyTariff2() const
{
    return getFloat(OBIS_IMPORTED_ENERGY_TARIFF_2);
}


float P1Reader::exportedEnergyTariff1() const
{
    return getFloat(OBIS_EXPORTED_ENERGY_TARIFF_1);
}


float P1Reader::exportedEnergyTariff2() const
{
    return getFloat(OBIS_EXPORTED_ENERGY_TARIFF_2);
}


// ============================================================
// Power
// ============================================================

float P1Reader::importedPower() const
{
    return getFloat(OBIS_IMPORTED_POWER);
}


float P1Reader::exportedPower() const
{
    return getFloat(OBIS_EXPORTED_POWER);
}


float P1Reader::netPower() const
{
    return importedPower() - exportedPower();
}


// ============================================================
// Voltage
// ============================================================

float P1Reader::voltagePhase1() const
{
    return getFloat(OBIS_VOLTAGE_PHASE_1);
}


float P1Reader::voltagePhase2() const
{
    return getFloat(OBIS_VOLTAGE_PHASE_2);
}


float P1Reader::voltagePhase3() const
{
    return getFloat(OBIS_VOLTAGE_PHASE_3);
}


// ============================================================
// Current
// ============================================================

float P1Reader::currentPhase1() const
{
    return getFloat(OBIS_CURRENT_PHASE_1);
}


float P1Reader::currentPhase2() const
{
    return getFloat(OBIS_CURRENT_PHASE_2);
}


float P1Reader::currentPhase3() const
{
    return getFloat(OBIS_CURRENT_PHASE_3);
}


// ============================================================
// Process character
// ============================================================

void P1Reader::processCharacter(char c)
{
    // --------------------------------------------------------
    // Optional byte-level debug.
    // --------------------------------------------------------

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
    //
    // A new '/' always starts a new telegram. This also
    // recovers from an incomplete previous telegram.
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

        // A new telegram supersedes the previous notification.
        dataAvailable_ = false;

        resetTelegram();

        receiving_ = true;

        telegram_ += c;

        lastCharacterTime_ = millis();

        return;
    }


    // --------------------------------------------------------
    // Ignore bytes outside a telegram.
    //
    // This also ignores CR/LF after a completed telegram.
    // --------------------------------------------------------

    if (!receiving_)
    {
        return;
    }


    // --------------------------------------------------------
    // After '!' we expect exactly four hexadecimal CRC
    // characters.
    // --------------------------------------------------------

    if (readingCrc_)
    {
        const bool isHex =
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');


        // ----------------------------------------------------
        // Anything other than a hexadecimal CRC character
        // invalidates this telegram.
        // ----------------------------------------------------

        if (!isHex)
        {
            ++invalidTelegramCount_;

            if (Settings::P1::DEBUG_LEVEL >= 1)
            {
                Serial.printf(
                    "P1: INVALID CRC character 0x%02X\n",
                    static_cast<unsigned int>(
                        static_cast<uint8_t>(c)
                    )
                );
            }

            resetTelegram();

            return;
        }


        // ----------------------------------------------------
        // Protect buffer before appending CRC character.
        // ----------------------------------------------------

        if (
            telegram_.length() >=
            Settings::P1::BUFFER_SIZE - 1
        )
        {
            ++bufferOverflowCount_;
            ++invalidTelegramCount_;

            if (Settings::P1::DEBUG_LEVEL >= 1)
            {
                Serial.println(
                    "P1: BUFFER OVERFLOW while reading CRC"
                );
            }

            resetTelegram();

            return;
        }


        telegram_ += c;

        ++crcCharactersRead_;

        lastCharacterTime_ = millis();


        // ----------------------------------------------------
        // Exactly four CRC characters received.
        // ----------------------------------------------------

        if (crcCharactersRead_ == 4)
        {
            ++telegramEndCount_;

            if (Settings::P1::DEBUG_LEVEL >= 1)
            {
                Serial.println(
                    "P1: END '!'+4 CRC characters detected"
                );
            }

            processTelegram();

            // Keep rawTelegram_, parsed values and
            // dataAvailable_. Reset only reception state.
            resetTelegram();
        }

        return;
    }


    // --------------------------------------------------------
    // Protect telegram buffer.
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
    // End marker.
    //
    // '!' is part of the CRC-covered data and therefore MUST
    // be stored in telegram_.
    // --------------------------------------------------------

    if (c == '!')
    {
        telegram_ += c;

        readingCrc_ = true;
        crcCharactersRead_ = 0;

        lastCharacterTime_ = millis();

        return;
    }


    // --------------------------------------------------------
    // Normal telegram character.
    // --------------------------------------------------------

    telegram_ += c;

    lastCharacterTime_ = millis();
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

        Serial.println();
        Serial.println(
            "================================="
        );
    }


    // --------------------------------------------------------
    // Validate CRC before accepting telegram.
    // --------------------------------------------------------

    if (!validateCRC(telegram_))
    {
        ++invalidTelegramCount_;
        ++crcErrorCount_;

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
// Reset current telegram reception
// ============================================================

void P1Reader::resetTelegram()
{
    telegram_.clear();

    receiving_ = false;

    readingCrc_ = false;

    crcCharactersRead_ = 0;

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
// CRC validation
// ============================================================

bool P1Reader::validateCRC(
    const String& telegram
)
{
    const int exclamation =
        telegram.indexOf('!');


    // --------------------------------------------------------
    // A valid telegram must contain '!'.
    // --------------------------------------------------------

    if (exclamation < 0)
    {
        return false;
    }


    // --------------------------------------------------------
    // The telegram must be exactly:
    //
    //     <data>!<4 hexadecimal CRC characters>
    //
    // No CR/LF or additional bytes are included.
    // --------------------------------------------------------

    const size_t expectedLength =
        static_cast<size_t>(
            exclamation + 5
        );


    if (telegram.length() != expectedLength)
    {
        if (Settings::P1::DEBUG_LEVEL >= 2)
        {
            Serial.printf(
                "P1 CRC: invalid length, "
                "expected=%u actual=%u\n",
                static_cast<unsigned int>(
                    expectedLength
                ),
                static_cast<unsigned int>(
                    telegram.length()
                )
            );
        }

        return false;
    }


    // --------------------------------------------------------
    // Extract the four CRC characters.
    // --------------------------------------------------------

    const String crcString =
        telegram.substring(
            exclamation + 1,
            exclamation + 5
        );


    // --------------------------------------------------------
    // Verify that all four characters are hexadecimal.
    // --------------------------------------------------------

    for (size_t i = 0; i < 4; ++i)
    {
        const char c = crcString[i];

        const bool isHex =
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');


        if (!isHex)
        {
            if (Settings::P1::DEBUG_LEVEL >= 2)
            {
                Serial.printf(
                    "P1 CRC: invalid hexadecimal "
                    "character '%c'\n",
                    c
                );
            }

            return false;
        }
    }


    // --------------------------------------------------------
    // Convert received CRC.
    // --------------------------------------------------------

    lastReceivedCrc_ =
        static_cast<uint16_t>(
            strtoul(
                crcString.c_str(),
                nullptr,
                16
            )
        );


    // --------------------------------------------------------
    // Calculate CRC over everything through '!'.
    //
    // The four CRC characters themselves are excluded.
    // --------------------------------------------------------

    lastCalculatedCrc_ =
        calculateCRC(
            reinterpret_cast<const uint8_t*>(
                telegram.c_str()
            ),
            static_cast<size_t>(
                exclamation + 1
            )
        );


    // --------------------------------------------------------
    // Report the CRC-covered telegram length.
    //
    // This is from '/' through '!'.
    // --------------------------------------------------------

    lastTelegramLength_ =
        static_cast<uint32_t>(
            exclamation + 1
        );


    if (Settings::P1::DEBUG_LEVEL >= 2)
    {
        Serial.printf(
            "P1 CRC: received=%04X "
            "calculated=%04X "
            "length=%lu\n",
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
// CRC-16
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