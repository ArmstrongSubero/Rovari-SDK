/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_flash.c - Internal Flash Read/Write for User Data Storage
 *
 * CH32V307 flash:
 *   - Page size: 4096 bytes (4 KB)
 *   - Erased value: 0xE339 per half-word (NOT 0xFFFF)
 *   - Minimum write unit: 16-bit half-word
 *   - Must erase before writing
 */

#include "rovari_flash.h"
#include "ch32v30x.h"
#include <string.h>

/* ---------------------------------------------------------------------- */
uint32_t flash_end_addr(void)
{
    uint16_t flash_kb = *(volatile uint16_t *)0x1FFFF7E0;
    return FLASH_BASE_ADDR + (uint32_t)flash_kb * 1024;
}

/* ---------------------------------------------------------------------- */
uint8_t flash_erase_page(uint32_t page_addr)
{
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    status = FLASH_ErasePage(page_addr);

    FLASH_Lock();

    return (status == FLASH_COMPLETE) ? 0 : 1;
}

/* ---------------------------------------------------------------------- */
uint8_t flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    FLASH_Status status;
    uint16_t i;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    /* Write 16-bit half-words */
    for (i = 0; i + 1 < len; i += 2)
    {
        uint16_t hw = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
        status = FLASH_ProgramHalfWord(addr + i, hw);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 1;
        }
    }

    /* Handle odd trailing byte: pad with 0xFF */
    if (i < len)
    {
        uint16_t hw = (uint16_t)data[i] | 0xFF00;
        status = FLASH_ProgramHalfWord(addr + i, hw);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 1;
        }
    }

    FLASH_Lock();
    return 0;
}

/* ---------------------------------------------------------------------- */
void flash_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    memcpy(buf, (const void *)addr, len);
}

/* ---------------------------------------------------------------------- */
uint8_t flash_write_word(uint32_t addr, uint32_t value)
{
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    status = FLASH_ProgramWord(addr, value);

    FLASH_Lock();

    return (status == FLASH_COMPLETE) ? 0 : 1;
}

/* ---------------------------------------------------------------------- */
uint32_t flash_read_word(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* ---------------------------------------------------------------------- */
uint8_t flash_is_page_erased(uint32_t page_addr)
{
    uint32_t *p = (uint32_t *)page_addr;
    uint16_t words = FLASH_PAGE_SIZE / 4;
    uint16_t i;

    for (i = 0; i < words; i++)
    {
        if (p[i] != FLASH_ERASED_WORD)
            return 0;
    }
    return 1;
}