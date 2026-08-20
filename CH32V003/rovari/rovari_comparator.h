/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_comparator.h - Analog comparator / op-amp (OPA) for CH32V003
 *
 * The CH32V003 OPA block serves as both comparator and op-amp.
 * Fixed pin assignment:
 *   PA2 = non-inverting input (V+)
 *   PA1 = inverting input (V-)
 *   PD4 = output
 *
 * Comparator mode:
 *   comparator_init();
 *   if (comparator_read()) { ... }  // PA2 > PA1, digital output
 *
 * Op-amp buffer mode (unity gain voltage follower):
 *   comparator_init();
 *   // wire PD4 back to PA1 externally
 *   // PD4 now outputs a buffered copy of PA2
 *
 * Op-amp amplifier mode:
 *   comparator_init();
 *   // connect Rf between PD4 and PA1
 *   // connect Rg between PA1 and GND
 *   // gain = 1 + Rf/Rg
 */

#ifndef ROVARI_COMPARATOR_H
#define ROVARI_COMPARATOR_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the OPA.
 * Configures PA1 and PA2 as analog inputs, PD4 as output.
 * Works for both comparator and op-amp modes.
 */
void comparator_init(void);

/**
 * Read the output as digital (comparator mode).
 * @return 1 if PA2 (V+) > PA1 (V-), 0 otherwise.
 */
uint8_t comparator_read(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Comparator {
public:
    Comparator()
    {
        comparator_init();
    }

    uint8_t read()     { return comparator_read(); }
    bool isHigh()      { return comparator_read() != 0; }
};

#endif

#endif /* ROVARI_COMPARATOR_H */
