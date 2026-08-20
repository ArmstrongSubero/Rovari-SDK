/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_rng.c
 * @brief Hardware random number generator for CH32V307.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"

/* Project includes (WCH HAL). ch32v30x_conf.h omits ch32v30x_rng.h, so it is
 * included explicitly here (see VENDOR_QUIRKS). */
#include "ch32v30x.h"
#include "ch32v30x_rng.h"

/* Bounded data-ready poll cap. At 144 MHz this is well under 1 ms. */
#define RNG_TIMEOUT  100000U

/**
 * @brief Enable the RNG clock and peripheral.
 * @req REQ-ROVARI-RNG-0010
 */
void rng_init(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
}

/**
 * @brief Read a 32-bit hardware random word.
 *
 * Polling for the data-ready flag is bounded so a non-responsive RNG cannot
 * hang the CPU; on timeout the last-read value (0) is returned.
 *
 * @return 32-bit random word, or 0 if the RNG did not become ready.
 * @req REQ-ROVARI-RNG-0011
 * @req REQ-ROVARI-RNG-0020
 */
uint32_t rng_read(void)
{
    for (uint32_t i = 0U; i < RNG_TIMEOUT; i++) {
        if (RNG_GetFlagStatus(RNG_FLAG_DRDY) != RESET) {
            return RNG_GetRandomNumber();
        }
    }
    return 0U;  /* Timeout: RNG not ready */
}

/**
 * @brief Return a random value in the inclusive range [min, max].
 * @param[in] min Lower bound.
 * @param[in] max Upper bound.
 * @return A value in [min, max], or min if min >= max.
 * @req REQ-ROVARI-RNG-0012
 */
uint32_t rng_range(uint32_t min, uint32_t max)
{
    if (min >= max) {
        return min;
    }
    uint32_t span = max - min + 1U;
    SEVS_INVARIANT(span > 0U);          /* guards the modulo against div-by-zero */
    uint32_t result = min + (rng_read() % span);
    SEVS_INVARIANT(result >= min && result <= max);
    return result;
}

/**
 * @brief Fill a buffer with random bytes.
 * @param[out] buf Destination buffer.
 * @param[in]  len Number of bytes to write; 0 is a no-op.
 * @req REQ-ROVARI-RNG-0013
 */
void rng_fill(uint8_t *buf, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);

    uint16_t i = 0;
    /* Bounded by len: each pass writes 1-4 bytes and advances i. */
    for (uint16_t guard = 0U; guard < len && i < len; guard++) {
        uint32_t val = rng_read();
        uint8_t remaining = (uint8_t)((len - i < 4) ? (len - i) : 4);
        for (uint8_t j = 0; j < remaining; j++) {
            buf[i++] = (uint8_t)(val & 0xFFU);
            val >>= 8;
        }
    }
}

/**
 * @brief Disable the RNG peripheral and its clock.
 * @req REQ-ROVARI-RNG-0014
 */
void rng_stop(void)
{
    RNG_Cmd(DISABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_RNG, DISABLE);
}
