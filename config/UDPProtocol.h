#pragma once

#include <Arduino.h>


namespace UDPProtocol
{
    // ========================================================
    // Protocol version
    // ========================================================

    constexpr uint8_t VERSION = 1;


    // ========================================================
    // P1 metrics
    //
    // All values are represented as IEEE-754 32-bit floats.
    //
    // The structure is packed so that its binary representation
    // is identical on both sides of the UDP connection.
    // ========================================================

    struct __attribute__((packed)) P1Metrics
    {
        // ----------------------------------------------------
        // Energy
        // ----------------------------------------------------

        float importedEnergyTariff1;
        float importedEnergyTariff2;

        float exportedEnergyTariff1;
        float exportedEnergyTariff2;


        // ----------------------------------------------------
        // Power
        // ----------------------------------------------------

        float importedPower;
        float exportedPower;
        float netPower;


        // ----------------------------------------------------
        // Voltage
        // ----------------------------------------------------

        float voltagePhase1;
        float voltagePhase2;
        float voltagePhase3;


        // ----------------------------------------------------
        // Current
        // ----------------------------------------------------

        float currentPhase1;
        float currentPhase2;
        float currentPhase3;
    };


    // ========================================================
    // P1 metrics size
    // ========================================================

    constexpr size_t P1_METRICS_SIZE =
        sizeof(P1Metrics);


    static_assert(
        sizeof(float) == 4,
        "UDP protocol requires 32-bit float"
    );


    static_assert(
        sizeof(P1Metrics) == 52,
        "Unexpected P1Metrics size"
    );


    // ========================================================
    // Encrypted payload
    //
    //   sequence
    //   timestamp
    //   P1Metrics
    // ========================================================

    constexpr size_t ENCRYPTED_METADATA_SIZE =
        sizeof(uint64_t) +    // sequence
        sizeof(uint64_t);     // timestamp


    constexpr size_t ENCRYPTED_PAYLOAD_SIZE =
        ENCRYPTED_METADATA_SIZE +
        P1_METRICS_SIZE;
}