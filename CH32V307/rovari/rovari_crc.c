/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_crc.c
 * @brief Hardware CRC-32 calculator for CH32V307 (SEVS-Core conformant).
 *
 * Wraps the CH32V307 hardware CRC unit (Ethernet polynomial 0x04C11DB7).
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "ch32v30x.h"
#include "rovari_crc.h"

/**
 * @brief Enable the hardware CRC peripheral clock.
 * @req REQ-ROVARI-CRC-0010
 */
void crc_init(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);
}

/**
 * @brief Reset the CRC data register to its initial value.
 * @req REQ-ROVARI-CRC-0011
 */
void crc_reset(void)
{
    CRC_ResetDR();
}

/**
 * @brief Feed a single 32-bit word into the CRC engine.
 * @param[in] data 32-bit value to accumulate.
 * @return Running CRC-32 result after this word.
 * @req REQ-ROVARI-CRC-0012
 */
uint32_t crc_feed(uint32_t data)
{
    return CRC_CalcCRC(data);
}

/**
 * @brief Calculate CRC-32 over a block of 32-bit words.
 *
 * Resets the CRC engine before calculation.
 *
 * @param[in] buf Array of 32-bit words.
 * @param[in] len Number of 32-bit words (not bytes).
 * @return CRC-32 checksum, or 0 if buf is NULL or len is zero.
 * @req REQ-ROVARI-CRC-0013
 * @req REQ-ROVARI-CRC-0020
 */
uint32_t crc_calculate(uint32_t *buf, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    if (len == 0U) {
        return 0U;
    }
    CRC_ResetDR();
    return CRC_CalcBlockCRC(buf, len);
}

/**
 * @brief Get the current CRC result without feeding new data.
 * @return Current accumulated CRC-32 value.
 *
 * @note Reads CRC->DATAR directly: the WCH HAL CRC_GetCRC() reads the ID
 *       register (CRC->IDATAR) by mistake and returns a stale value.
 * @req REQ-ROVARI-CRC-0014
 * @req REQ-ROVARI-CRC-WORKAROUND-001
 */
uint32_t crc_get(void)
{
    SEVS_INVARIANT(CRC != NULL);
    return CRC->DATAR;
}

/**
 * @brief Store a user-defined 8-bit value in the CRC ID register.
 * @param[in] id 8-bit value to store.
 * @req REQ-ROVARI-CRC-0015
 */
void crc_set_id(uint8_t id)
{
    CRC_SetIDRegister(id);
}

/**
 * @brief Read the user-defined 8-bit value from the CRC ID register.
 * @return Stored 8-bit value.
 * @req REQ-ROVARI-CRC-0015
 */
uint8_t crc_get_id(void)
{
    return CRC_GetIDRegister();
}
