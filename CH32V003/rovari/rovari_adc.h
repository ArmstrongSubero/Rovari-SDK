/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_adc.h - ADC abstraction for CH32V003 (10-bit, ADC1 only)
 *
 * ADC channels:
 *   0: PA2    1: PA1    2: PC4    3: PD2
 *   4: PD3    5: PD5    6: PD6    7: PD4
 *   8: Vrefint (internal 1.2V reference)
 *   9: Vcalint (internal calibration voltage)
 */

#ifndef ROVARI_ADC_H
#define ROVARI_ADC_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void adc_init(pin_t pin);
uint16_t analog_read(pin_t pin);
uint16_t analog_read_mv(pin_t pin);
void adc_set_sample_time(pin_t pin, uint8_t cycles);
void adc_init_internal(void);
uint16_t adc_read_vrefint_mv(void);
uint16_t adc_read_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Adc {
public:
    Adc(pin_t pin) : _pin(pin) {
        adc_init(pin);
    }
    uint16_t read()     { return analog_read(_pin); }
    uint16_t readMv()   { return analog_read_mv(_pin); }
private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_ADC_H */
