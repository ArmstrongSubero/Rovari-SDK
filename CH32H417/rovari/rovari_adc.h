/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_adc.h - Analog-to-Digital Converter (CH32H417)
 *
 * 12-bit ADC, 16 external channels + 2 internal channels.
 *
 * External ADC pins:
 *   PA0-PA7  -> channels 0-7
 *   PB0-PB1  -> channels 8-9
 *   PC0-PC5  -> channels 10-15
 *
 * Internal channels:
 *   Channel 16 -> temperature sensor
 *   Channel 17 -> Vrefint (~1.2V)
 *
 * Usage:
 *   adc_init(PC0);
 *   uint16_t raw = analog_read(PC0);        // 0-4095
 *   float volts = analog_read_voltage(PC0);  // 0.0-3.3V
 */

#ifndef ROVARI_ADC_H
#define ROVARI_ADC_H

#include "rovari_defs.h"

/* =========================================================================
 *  C API
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize an ADC pin for analog input.
 * Configures the pin as analog mode and initializes the ADC
 * peripheral (calibration runs once on first call).
 *
 *   adc_init(PC0);   // PC0 = ADC channel 10
 */
void adc_init(pin_t pin);

/**
 * Read a 12-bit ADC value (0-4095).
 * Blocking: waits for conversion to complete (~2-5 us).
 *
 *   uint16_t raw = analog_read(PC0);
 */
uint16_t analog_read(pin_t pin);

/**
 * Read the ADC value as a voltage (0.0 - 3.3V).
 * Assumes VREF+ = 3.3V.
 *
 *   float v = analog_read_voltage(PC0);
 */
float analog_read_voltage(pin_t pin);

/**
 * Set the ADC sample time for a pin's channel.
 * Longer sample times give more accurate readings on high-impedance
 * sources at the cost of speed. Pass one of the ADC_SampleTime_* constants.
 * Default if never set: ADC_SampleTime_CyclesMode5.
 *
 *   adc_set_sample_time(PC0, ADC_SampleTime_CyclesMode5);
 */
void adc_set_sample_time(pin_t pin, uint8_t cycles);

/**
 * Initialize internal ADC channels (temperature sensor, Vrefint).
 * Call once before using adc_read_temperature() or adc_read_vrefint().
 */
void adc_init_internal(void);

/**
 * Read the internal temperature sensor in degrees Celsius.
 * Call adc_init_internal() first.
 */
float adc_read_temperature(void);

/**
 * Read the internal voltage reference (~1.2V).
 * Call adc_init_internal() first.
 */
float adc_read_vrefint(void);

/**
 * Read a raw ADC channel by number (0-17).
 * For advanced use when you need direct channel access.
 */
uint16_t adc_read_channel(uint8_t channel);

/**
 * Read the ADC value in millivolts (0-3300).
 * Cross-target compatible with CH32V003/V307.
 */
static inline uint16_t analog_read_mv(pin_t pin) {
    return (uint16_t)(analog_read_voltage(pin) * 1000.0f);
}

/**
 * Read the internal temperature in tenths of a degree Celsius.
 * Cross-target compatible with CH32V307.
 * Call adc_init_internal() first.
 */
static inline int16_t adc_read_temperature_c10(void) {
    return (int16_t)(adc_read_temperature() * 10.0f);
}

/**
 * Read the internal voltage reference in millivolts.
 * Cross-target compatible with CH32V003/V307.
 * Call adc_init_internal() first.
 */
static inline uint16_t adc_read_vrefint_mv(void) {
    return (uint16_t)(adc_read_vrefint() * 1000.0f);
}

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_ADC_H */
