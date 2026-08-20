/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_crc.c - Hardware CRC32 Calculator (CH32H417)
 */

#include "rovari_crc.h"
#include "debug.h"

void crc_init(void)
{
    RCC_HBPeriphClockCmd(RCC_HBPeriph_CRC, ENABLE);
}

void crc_reset(void)
{
    CRC_ResetDR();
}

uint32_t crc_feed(uint32_t data)
{
    return CRC_CalcCRC(data);
}

uint32_t crc_calculate(uint32_t *buf, uint32_t len)
{
    CRC_ResetDR();
    return CRC_CalcBlockCRC(buf, len);
}

uint32_t crc_get(void)
{
    /* WCH HAL bug: CRC_GetCRC() reads IDATAR instead of DATAR.
     * Read the data register directly. */
    return CRC->DATAR;
}

void crc_set_id(uint8_t id)
{
    CRC_SetIDRegister(id);
}

uint8_t crc_get_id(void)
{
    return CRC_GetIDRegister();
}
