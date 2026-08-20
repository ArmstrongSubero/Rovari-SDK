/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_dac.h
 * @brief 12-bit dual-channel DAC for CH32V307 (integer interface).
 *
 * DAC Channel 1 = PA4, Channel 2 = PA5, output range 0-3300 mV.
 * The output is unbuffered by default; an optional internal buffer can
 * drive low-impedance loads at the cost of not reaching the rails.
 *
 * All interfaces are integer. Voltage is expressed in millivolts and
 * percentage as a whole-number 0-100, consistent with the SEVS no-float
 * convention for driver code.
 *
 *   dac_init(PA4);              // Init DAC channel 1
 *   dac_write(PA4, 2048);       // ~1650 mV (mid-scale)
 *   dac_write_mv(PA4, 1000);    // 1000 mV
 *   dac_write_pct(PA4, 75);     // 75% of full scale (~2475 mV)
 */

#ifndef ROVARI_DAC_H
#define ROVARI_DAC_H

#include "rovari_defs.h"

/** DAC reference voltage in millivolts (VREF+). */
#define DAC_VREF_MV    3300U
/** Maximum 12-bit DAC code. */
#define DAC_MAX_VALUE  4095U

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a DAC output channel.
 *
 * Configures the pin as analog, enables the DAC clock, and starts the
 * channel with output at 0. Valid pins are PA4 (channel 1) and PA5
 * (channel 2); other pins are ignored.
 *
 * @param[in] pin PA4 or PA5.
 * @req REQ-ROVARI-DAC-0010
 */
void dac_init(pin_t pin);

/**
 * @brief Set DAC output using a 12-bit value (0-4095).
 * @param[in] pin   PA4 or PA5.
 * @param[in] value 12-bit output value; values above 4095 are clamped.
 * @req REQ-ROVARI-DAC-0011
 */
void dac_write(pin_t pin, uint16_t value);

/**
 * @brief Set DAC output to a target voltage in millivolts.
 *
 * Integer conversion: value = mv * 4095 / 3300. Inputs above 3300 mV are
 * clamped to full scale.
 *
 * @param[in] pin PA4 or PA5.
 * @param[in] mv  Desired output in millivolts (0-3300).
 * @req REQ-ROVARI-DAC-0012
 */
void dac_write_mv(pin_t pin, uint16_t mv);

/**
 * @brief Set DAC output as an integer percentage of full scale.
 * @param[in] pin     PA4 or PA5.
 * @param[in] percent Percentage 0-100; values above 100 are clamped.
 * @req REQ-ROVARI-DAC-0013
 */
void dac_write_pct(pin_t pin, uint8_t percent);

/**
 * @brief Enable or disable the internal output buffer on a channel.
 *
 * The buffer drives lower-impedance loads (down to ~5 kOhm) at the cost of
 * not reaching the true 0 V or 3.3 V rails. Off by default.
 *
 * @param[in] pin    PA4 or PA5.
 * @param[in] enable Non-zero enables the buffer; zero disables it.
 * @req REQ-ROVARI-DAC-0014
 */
void dac_buffer(pin_t pin, uint8_t enable);

/**
 * @brief Stop the DAC channel and return the pin to GPIO output low.
 * @param[in] pin PA4 or PA5.
 * @req REQ-ROVARI-DAC-0015
 */
void dac_stop(pin_t pin);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus

/**
 * Dac is the C++ wrapper for the Rovari DAC API (integer interface).
 *
 * Usage:
 *   Dac output(PA4);
 *   output.begin();
 *   output.write(2048);
 *   output.writeMv(1650);
 */
class Dac {
public:
    explicit Dac(pin_t pin) : _pin(pin) {}

    /** Initialize this DAC channel. */
    void begin()                      { dac_init(_pin); }

    /** Write raw 12-bit value (0-4095). */
    void write(uint16_t value)        { dac_write(_pin, value); }

    /** Write an output in millivolts (0-3300). */
    void writeMv(uint16_t mv)         { dac_write_mv(_pin, mv); }

    /** Write an integer percentage (0-100). */
    void writePct(uint8_t percent)    { dac_write_pct(_pin, percent); }

    /** Enable or disable the output buffer. */
    void buffer(uint8_t enable)       { dac_buffer(_pin, enable); }

    /** Stop the DAC and return pin to GPIO. */
    void stop()                       { dac_stop(_pin); }

    /** Get the pin this Dac is bound to. */
    pin_t pin() const                 { return _pin; }

private:
    pin_t _pin;
};

#endif /* __cplusplus */

#endif /* ROVARI_DAC_H */
