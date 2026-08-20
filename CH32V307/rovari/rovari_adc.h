/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_adc.h
 * @brief 12-bit ADC on ADC1 for CH32V307 (integer interface).
 *
 * Single-conversion, software-triggered reads. Fixed pin-to-channel
 * mapping per datasheet. All interfaces are integer: voltage in millivolts,
 * temperature in tenths of a degree Celsius, per the SEVS no-float
 * convention for driver code.
 *
 *   adc_init(PA0);                          // Init PA0 as analog input
 *   uint16_t raw = analog_read(PA0);        // 0-4095
 *   uint16_t mv  = analog_read_mv(PA0);     // 0-3300 mV
 *   adc_init_internal();                    // Enable temp sensor + Vrefint
 *   int16_t  t10 = adc_read_temperature_c10(); // tenths of degC
 */

#ifndef ROVARI_ADC_H
#define ROVARI_ADC_H

#include "rovari_defs.h"

/** ADC reference voltage in millivolts (VREF+). */
#define ADC_VREF_MV    3300U
/** Maximum 12-bit ADC code. */
#define ADC_MAX_VALUE  4095U

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a pin for ADC input (one-time ADC1 init on first call).
 * @param[in] pin ADC-capable pin (PA0-PA7, PB0-PB1, PC0-PC5).
 * @req REQ-ROVARI-ADC-0010
 */
void adc_init(pin_t pin);

/**
 * @brief Read the raw 12-bit conversion for a pin.
 * @param[in] pin ADC-capable pin.
 * @return 0-4095, or 0 if the pin is not ADC-capable.
 * @req REQ-ROVARI-ADC-0011
 */
uint16_t analog_read(pin_t pin);

/**
 * @brief Read a pin's voltage in millivolts.
 * @param[in] pin ADC-capable pin.
 * @return Voltage in millivolts (0-3300).
 * @req REQ-ROVARI-ADC-0012
 */
uint16_t analog_read_mv(pin_t pin);

/**
 * @brief Override the per-channel ADC sample time for a pin.
 * @param[in] pin    ADC-capable pin.
 * @param[in] cycles ADC_SampleTime_xxx value.
 * @req REQ-ROVARI-ADC-0013
 */
void adc_set_sample_time(pin_t pin, uint8_t cycles);

/**
 * @brief Enable the internal temperature sensor and reference channels.
 * @req REQ-ROVARI-ADC-0014
 */
void adc_init_internal(void);

/**
 * @brief Read the die temperature in tenths of a degree Celsius.
 * @return Temperature in 0.1 degC units (e.g. 253 means 25.3 degC).
 * @req REQ-ROVARI-ADC-0015
 */
int16_t adc_read_temperature_c10(void);

/**
 * @brief Read the internal reference voltage in millivolts.
 * @return Vrefint in millivolts.
 * @req REQ-ROVARI-ADC-0016
 */
uint16_t adc_read_vrefint_mv(void);

/**
 * @brief Read an explicit ADC channel number.
 * @param[in] channel Channel 0-17.
 * @return 0-4095, or 0 if the channel is out of range.
 * @req REQ-ROVARI-ADC-0017
 */
uint16_t adc_read_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus

/**
 * Adc is the C++ wrapper for the Rovari ADC API (integer interface).
 *
 * Usage:
 *   Adc sensor(PA0);
 *   sensor.begin();
 *   uint16_t raw = sensor.read();
 *   uint16_t mv  = sensor.readMv();
 */
class Adc {
public:
    explicit Adc(pin_t pin) : _pin(pin) {}

    /** Initialize this pin for ADC input. */
    void begin()                       { adc_init(_pin); }

    /** Read raw 12-bit value (0-4095). */
    uint16_t read()                    { return analog_read(_pin); }

    /** Read voltage in millivolts (0-3300). */
    uint16_t readMv()                  { return analog_read_mv(_pin); }

    /** Set sample time. */
    void setSampleTime(uint8_t cycles) { adc_set_sample_time(_pin, cycles); }

    /** Get the pin this Adc is bound to. */
    pin_t pin() const                  { return _pin; }

private:
    pin_t _pin;
};

#endif /* __cplusplus */

#endif /* ROVARI_ADC_H */
