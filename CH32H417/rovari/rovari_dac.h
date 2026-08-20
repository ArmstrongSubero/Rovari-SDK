/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_dac.h - 12-bit DAC (Digital-to-Analog Converter)
 *
 * The CH32H417 has a dual-channel 12-bit DAC.  Each channel converts a
 * digital value (0–4095) to an analog voltage on a fixed output pin:
 *
 *   DAC Channel 1 = PA4   (0.0 V – 3.3 V)
 *   DAC Channel 2 = PA5   (0.0 V – 3.3 V)
 *
 * The DAC output is unbuffered by default.  An optional internal output
 * buffer can drive low-impedance loads directly (down to ~5 KΩ) at the
 * cost of not reaching the true rail voltages (output range shrinks to
 * approximately 0.2 V – 3.1 V).
 *
 * Usage:
 *   dac_init(PA4);                   // Init DAC channel 1
 *   dac_write(PA4, 2048);           // Output ~1.65 V (mid-scale)
 *   dac_write_voltage(PA4, 1.0f);   // Output 1.0 V
 *   dac_write_pct(PA4, 75.0f);      // Output 75% of 3.3 V ≈ 2.475 V
 */

#ifndef ROVARI_DAC_H
#define ROVARI_DAC_H

#include "rovari_defs.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  C API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a DAC output channel.
 *
 * Configures the pin as analog (disconnects digital buffer),
 * enables the DAC clock, and starts the channel with output at 0 V.
 *
 * Only two pins are valid:
 *   PA4 - DAC channel 1
 *   PA5 - DAC channel 2
 *
 * @param pin   PA4 or PA5
 */
void dac_init(pin_t pin);

/**
 * Set DAC output using a 12-bit value (0–4095).
 *   0    → 0.0 V (ground)
 *   2048 → ~1.65 V (mid-scale)
 *   4095 → ~3.3 V (VREF+)
 *
 * @param pin    PA4 or PA5
 * @param value  12-bit output value (0–4095), clamped internally
 */
void dac_write(pin_t pin, uint16_t value);

/**
 * Set DAC output voltage (0.0–3.3 V).
 * Internally converts to a 12-bit value and writes to the DAC.
 *
 *   dac_write_voltage(PA4, 1.65f);   // Mid-scale
 *
 * @param pin      PA4 or PA5
 * @param voltage  Desired output voltage (clamped to 0.0–3.3)
 */
void dac_write_voltage(pin_t pin, float voltage);

/**
 * Set DAC output as a percentage of full scale (0.0–100.0%).
 *
 *   dac_write_pct(PA4, 50.0f);   // 50% → ~1.65 V
 *
 * @param pin      PA4 or PA5
 * @param percent  Percentage (0.0–100.0), clamped internally
 */
void dac_write_pct(pin_t pin, float percent);

/**
 * Enable the internal output buffer on a DAC channel.
 *
 * The buffer allows the DAC to drive lower-impedance loads directly
 * (down to ~5 KΩ) without an external op-amp.  Trade-off: the output
 * range shrinks - it cannot reach the true 0 V or 3.3 V rails.
 *
 * Buffer is OFF by default (full rail-to-rail swing for high-Z loads).
 *
 * @param pin     PA4 or PA5
 * @param enable  1 = enable buffer, 0 = disable buffer
 */
void dac_buffer(pin_t pin, uint8_t enable);

/**
 * Stop the DAC channel and return the pin to GPIO output low.
 *
 * @param pin   PA4 or PA5
 */
void dac_stop(pin_t pin);

/**
 * Set DAC output in millivolts (0-3300).
 * Cross-target compatible with CH32V307.
 *
 *   dac_write_mv(PA4, 1650);   // Output 1.65 V
 *
 * @param pin  PA4 or PA5
 * @param mv   Millivolts (0-3300), clamped internally
 */
static inline void dac_write_mv(pin_t pin, uint16_t mv) {
    dac_write_voltage(pin, (float)mv / 1000.0f);
}

#ifdef __cplusplus
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  C++ API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus

/**
 * Dac - C++ wrapper for the Rovari DAC API.
 *
 * Usage:
 *   Dac output(PA4);
 *   output.begin();
 *   output.write(2048);
 *   output.writeVoltage(1.65f);
 */
class Dac {
public:
    explicit Dac(pin_t pin) : _pin(pin) {}

    /** Initialize this DAC channel. */
    void begin()                        { dac_init(_pin); }

    /** Write raw 12-bit value (0–4095). */
    void write(uint16_t value)          { dac_write(_pin, value); }

    /** Write a voltage (0.0–3.3 V). */
    void writeVoltage(float voltage)    { dac_write_voltage(_pin, voltage); }

    /** Write in millivolts (0-3300). Cross-target compatible. */
    void writeMv(uint16_t mv)           { dac_write_mv(_pin, mv); }

    /** Write a percentage (0.0–100.0%). */
    void writePct(float percent)        { dac_write_pct(_pin, percent); }

    /** Enable or disable the output buffer. */
    void buffer(uint8_t enable)         { dac_buffer(_pin, enable); }

    /** Stop the DAC and return pin to GPIO. */
    void stop()                         { dac_stop(_pin); }

    /** Get the pin this Dac is bound to. */
    pin_t pin() const                   { return _pin; }

private:
    pin_t _pin;
};

#endif /* __cplusplus */

#endif /* ROVARI_DAC_H */
