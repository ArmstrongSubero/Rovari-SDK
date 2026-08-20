/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_rng.h - Hardware Random Number Generator
 *
 * The CH32V307 has a true hardware RNG peripheral that generates
 * 32-bit random numbers from analog noise sources.
 *
 * Usage:
 *   rng_init();
 *   uint32_t val = rng_read();           // 32-bit random number
 *   uint32_t bounded = rng_range(1, 6);  // 1 to 6 inclusive (dice roll)
 */

#ifndef ROVARI_RNG_H
#define ROVARI_RNG_H

#include <stdint.h>

/* ======================================================================
 *  C API
 * ====================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable the hardware RNG peripheral.
 * Enables AHB clock for RNG and starts the generator.
 */
void rng_init(void);

/**
 * Read a 32-bit true random number.
 * Blocks until the RNG data-ready flag is set.
 *
 * @return  32-bit random value
 */
uint32_t rng_read(void);

/**
 * Read a random number within an inclusive range.
 *
 * @param min  lower bound (inclusive)
 * @param max  upper bound (inclusive)
 * @return     random value in [min, max]
 */
uint32_t rng_range(uint32_t min, uint32_t max);

/**
 * Fill a buffer with random bytes.
 *
 * @param buf  destination buffer
 * @param len  number of bytes to fill
 */
void rng_fill(uint8_t *buf, uint16_t len);

/**
 * Stop the RNG peripheral to save power.
 */
void rng_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_RNG_H */
