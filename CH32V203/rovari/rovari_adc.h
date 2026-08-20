/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_adc.h — 12-bit ADC (Analog-to-Digital Converter)
 *
 * CH32V307 has two ADC units (ADC1, ADC2), each with 16 external channels
 * plus internal temperature sensor and Vrefint on ADC1.
 *
 * Default pin-to-channel mapping:
 *   ADC Channel 0  = PA0       ADC Channel 8  = PB0
 *   ADC Channel 1  = PA1       ADC Channel 9  = PB1
 *   ADC Channel 2  = PA2       ADC Channel 10 = PC0
 *   ADC Channel 3  = PA3       ADC Channel 11 = PC1
 *   ADC Channel 4  = PA4       ADC Channel 12 = PC2
 *   ADC Channel 5  = PA5       ADC Channel 13 = PC3
 *   ADC Channel 6  = PA6       ADC Channel 14 = PC4
 *   ADC Channel 7  = PA7       ADC Channel 15 = PC5
 *
 * Internal channels (ADC1 only):
 *   Channel 16 = Internal temperature sensor
 *   Channel 17 = Vrefint (1.2 V reference)
 *
 * Usage:
 *   adc_init(PA0);                        // Init PA0 as analog input
 *   uint16_t raw = analog_read(PA0);      // 0–4095
 *   float v = analog_read_voltage(PA0);   // 0.0–3.3 V
 *
 *   adc_init_internal();                  // Enable temp sensor + Vrefint
 *   float temp = adc_read_temperature();  // Degrees Celsius
 */

#ifndef ROVARI_ADC_H
#define ROVARI_ADC_H

#include "rovari_defs.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  C API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a pin for analog input (ADC1).
 * Configures the pin as analog mode, enables ADC1 clock, and
 * calibrates the ADC on first call.
 *
 *   adc_init(PA0);   // ADC1 channel 0
 *   adc_init(PB1);   // ADC1 channel 9
 *   adc_init(PC3);   // ADC1 channel 13
 *
 * @param pin   An ADC-capable pin (PA0–PA7, PB0–PB1, PC0–PC5)
 */
void adc_init(pin_t pin);

/**
 * Read a single ADC value (12-bit, 0–4095).
 * Performs a software-triggered single conversion on ADC1.
 *
 *   uint16_t raw = analog_read(PA0);
 *
 * @param pin   Previously initialized ADC pin
 * @return      12-bit conversion result (0–4095), or 0 if pin is invalid
 */
uint16_t analog_read(pin_t pin);

/**
 * Read an ADC pin and convert to voltage (0.0–3.3 V).
 *
 *   float v = analog_read_voltage(PA0);
 *   serial.printf("Voltage: %.2f V\n", v);
 *
 * @param pin   Previously initialized ADC pin
 * @return      Voltage in the range 0.0–3.3 V
 */
float analog_read_voltage(pin_t pin);

/**
 * Set the ADC sample time for a pin.
 * Longer sample times give more accurate readings for high-impedance sources.
 *
 * Allowed values (cycles):
 *   ADC_SampleTime_1Cycles5    ADC_SampleTime_7Cycles5
 *   ADC_SampleTime_13Cycles5   ADC_SampleTime_28Cycles5
 *   ADC_SampleTime_41Cycles5   ADC_SampleTime_55Cycles5
 *   ADC_SampleTime_71Cycles5   ADC_SampleTime_239Cycles5
 *
 * Default: ADC_SampleTime_239Cycles5 (slowest, most accurate)
 *
 * @param pin     ADC-capable pin
 * @param cycles  Sample time constant (e.g. ADC_SampleTime_55Cycles5)
 */
void adc_set_sample_time(pin_t pin, uint8_t cycles);

/**
 * Initialize internal ADC channels (ADC1 only).
 * Enables the internal temperature sensor and Vrefint (1.2 V).
 * Must be called before adc_read_temperature() or adc_read_vrefint().
 */
void adc_init_internal(void);

/**
 * Read the internal temperature sensor (degrees Celsius).
 * Call adc_init_internal() first.
 *
 * Note: The internal sensor is approximate (±1.5 °C typical).
 * For precision, use an external sensor like the DS18B20.
 *
 * @return  Die temperature in degrees Celsius
 */
float adc_read_temperature(void);

/**
 * Read the internal Vrefint channel (nominally 1.2 V).
 * Useful for calibrating readings when Vref+ is unknown.
 * Call adc_init_internal() first.
 *
 * @return  Vrefint voltage in volts (nominally ~1.2 V)
 */
float adc_read_vrefint(void);

/**
 * Read a raw 12-bit value from an ADC channel number (0–17).
 * For advanced users who want direct channel access.
 *
 * @param channel  ADC channel number (0–15 external, 16=temp, 17=Vrefint)
 * @return         12-bit conversion result (0–4095)
 */
uint16_t adc_read_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  C++ API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus

/**
 * Adc — C++ wrapper for the Rovari ADC API.
 *
 * Usage:
 *   Adc sensor(PA0);
 *   sensor.begin();
 *   uint16_t raw = sensor.read();
 *   float voltage = sensor.readVoltage();
 */
class Adc {
public:
    explicit Adc(pin_t pin) : _pin(pin) {}

    /** Initialize this pin for ADC input. */
    void begin()                       { adc_init(_pin); }

    /** Read raw 12-bit value (0–4095). */
    uint16_t read()                    { return analog_read(_pin); }

    /** Read voltage (0.0–3.3 V). */
    float readVoltage()                { return analog_read_voltage(_pin); }

    /** Set sample time. */
    void setSampleTime(uint8_t cycles) { adc_set_sample_time(_pin, cycles); }

    /** Get the pin this Adc is bound to. */
    pin_t pin() const                  { return _pin; }

private:
    pin_t _pin;
};

#endif /* __cplusplus */

#endif /* ROVARI_ADC_H */
