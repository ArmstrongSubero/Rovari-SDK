/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_flash.c
 * @brief Internal flash read/write for user data storage.
 *
 * CH32V307 flash: 4 KB page size, erased value 0xE339E339 per word (not
 * 0xFFFFFFFF), 16-bit half-word minimum write unit, erase-before-write.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sevs_runtime.h"
#include "ch32v30x.h"
#include "rovari_flash.h"

/**
 * @brief Return the end address of usable flash.
 * @return End address derived from the chip flash-size register.
 * @req REQ-ROVARI-FLASH-0010
 */
uint32_t flash_end_addr(void)
{
    uint16_t flash_kb = *(volatile uint16_t *)0x1FFFF7E0;
    SEVS_INVARIANT(flash_kb > 0U);
    return FLASH_BASE_ADDR + (uint32_t)flash_kb * 1024U;
}

/**
 * @brief Erase one 4 KB flash page.
 * @param[in] page_addr 4K-aligned page base address.
 * @return 0 on success, non-zero on failure.
 * @req REQ-ROVARI-FLASH-0011
 * @req REQ-ROVARI-FLASH-WORKAROUND-002
 */
uint8_t flash_erase_page(uint32_t page_addr)
{
    SEVS_INVARIANT((page_addr % FLASH_PAGE_SIZE) == 0U);
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    FLASH_Status status = FLASH_ErasePage(page_addr);

    FLASH_Lock();
    return (status == FLASH_COMPLETE) ? 0 : 1;
}

/**
 * @brief Program a byte buffer into flash as 16-bit half-words.
 * @param[in] addr Destination flash address (half-word aligned).
 * @param[in] data Source bytes.
 * @param[in] len  Number of bytes; an odd trailing byte is padded with 0xFF.
 * @return 0 on success, non-zero on failure.
 * @req REQ-ROVARI-FLASH-0012
 * @req REQ-ROVARI-FLASH-0020
 */
uint8_t flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    FLASH_Status status;
    uint16_t i;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    /* Write 16-bit half-words */
    for (i = 0; i + 1 < len; i += 2) {
        uint16_t hw = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
        status = FLASH_ProgramHalfWord(addr + i, hw);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return 1;
        }
    }

    /* Handle odd trailing byte: pad with 0xFF */
    if (i < len) {
        uint16_t hw = (uint16_t)data[i] | 0xFF00U;
        status = FLASH_ProgramHalfWord(addr + i, hw);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return 1;
        }
    }

    FLASH_Lock();
    return 0;
}

/**
 * @brief Copy bytes from flash into a caller buffer.
 * @param[in]  addr Source flash address.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Number of bytes to copy.
 * @req REQ-ROVARI-FLASH-0013
 * @req REQ-ROVARI-FLASH-0020
 */
void flash_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    memcpy(buf, (const void *)addr, len);
}

/**
 * @brief Program a single 32-bit word.
 * @param[in] addr  Destination flash address (word aligned).
 * @param[in] value Word to program.
 * @return 0 on success, non-zero on failure.
 * @req REQ-ROVARI-FLASH-0014
 */
uint8_t flash_write_word(uint32_t addr, uint32_t value)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    FLASH_Status status = FLASH_ProgramWord(addr, value);

    FLASH_Lock();
    return (status == FLASH_COMPLETE) ? 0 : 1;
}

/**
 * @brief Read a single 32-bit word from flash.
 * @param[in] addr Source flash address (word aligned).
 * @return The 32-bit word at addr.
 * @req REQ-ROVARI-FLASH-0014
 */
uint32_t flash_read_word(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/**
 * @brief Report whether a page reads entirely as the erased pattern.
 * @param[in] page_addr 4K-aligned page base address.
 * @return 1 if fully erased, 0 otherwise.
 *
 * @note Compares against FLASH_ERASED_WORD (0xE339E339): CH32V307 erased
 *       flash does not read as 0xFFFFFFFF.
 * @req REQ-ROVARI-FLASH-0015
 * @req REQ-ROVARI-FLASH-WORKAROUND-001
 */
uint8_t flash_is_page_erased(uint32_t page_addr)
{
    SEVS_INVARIANT((page_addr % FLASH_PAGE_SIZE) == 0U);
    const uint32_t *p = (const uint32_t *)page_addr;
    uint16_t words = FLASH_PAGE_SIZE / 4U;

    for (uint16_t i = 0; i < words; i++) {
        if (p[i] != FLASH_ERASED_WORD) {
            return 0;
        }
    }
    return 1;
}
