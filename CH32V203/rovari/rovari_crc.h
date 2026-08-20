/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_crc.h - Hardware CRC32 Calculator
 *
 * The CH32V307 has a dedicated hardware CRC calculation unit that computes
 * CRC-32 (Ethernet polynomial 0x04C11DB7) in a single clock cycle per word.
 *
 * Usage:
 *   crc_init();
 *   uint32_t checksum = crc_calculate(data, length);
 *
 *   // Incremental:
 *   crc_reset();
 *   crc_feed(word1);
 *   crc_feed(word2);
 *   uint32_t result = crc_get();
 */

#ifndef ROVARI_CRC_H
#define ROVARI_CRC_H

#include <stdint.h>

/* ======================================================================
 *  C API
 * ====================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enable the hardware CRC peripheral clock.
 */
void crc_init(void);

/**
 * Reset the CRC data register to 0xFFFFFFFF.
 */
void crc_reset(void);

/**
 * Feed a single 32-bit word into the CRC engine.
 *
 * @param data  32-bit value to accumulate
 * @return      current CRC-32 result after this word
 */
uint32_t crc_feed(uint32_t data);

/**
 * Calculate CRC-32 over a block of 32-bit words.
 * Resets the CRC engine before calculation.
 *
 * @param buf   pointer to array of uint32_t words
 * @param len   number of 32-bit words (NOT bytes)
 * @return      CRC-32 checksum
 */
uint32_t crc_calculate(uint32_t *buf, uint32_t len);

/**
 * Get the current CRC result without feeding new data.
 *
 * @return  current accumulated CRC-32 value
 */
uint32_t crc_get(void);

/**
 * Store a user-defined 8-bit value in the CRC ID register.
 * Useful for tagging data blocks with a version or identifier.
 *
 * @param id  8-bit value to store
 */
void crc_set_id(uint8_t id);

/**
 * Read the user-defined 8-bit value from the CRC ID register.
 *
 * @return  stored 8-bit value
 */
uint8_t crc_get_id(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_CRC_H */
