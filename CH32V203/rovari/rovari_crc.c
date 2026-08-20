/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_crc.c - Hardware CRC32 Calculator
 */

#include "rovari_crc.h"
#include "ch32v30x.h"

/* ---------------------------------------------------------------------- */
void crc_init(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);
}

/* ---------------------------------------------------------------------- */
void crc_reset(void)
{
    CRC_ResetDR();
}

/* ---------------------------------------------------------------------- */
uint32_t crc_feed(uint32_t data)
{
    return CRC_CalcCRC(data);
}

/* ---------------------------------------------------------------------- */
uint32_t crc_calculate(uint32_t *buf, uint32_t len)
{
    CRC_ResetDR();
    return CRC_CalcBlockCRC(buf, len);
}

/* ---------------------------------------------------------------------- */
uint32_t crc_get(void)
{
    /* Note: WCH's CRC_GetCRC() reads IDATAR (ID register) instead of
     * DATAR (CRC result register), this is a bug in their HAL.
     * Read the data register directly. */
    return CRC->DATAR;
}

/* ---------------------------------------------------------------------- */
void crc_set_id(uint8_t id)
{
    CRC_SetIDRegister(id);
}

/* ---------------------------------------------------------------------- */
uint8_t crc_get_id(void)
{
    return CRC_GetIDRegister();
}