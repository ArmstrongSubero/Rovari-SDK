/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_flash.h - Internal Flash Read/Write for User Data Storage
 *
 * The CH32V307 has 320 KB of unified memory split between Flash and RAM
 * (partition configured in link.ld, default: 256K Flash + 64K RAM).
 *
 * CH32V307 Flash Quirks:
 *   - Page size is 4096 bytes (4 KB), not 256 bytes.
 *   - Erased flash reads as 0xE339E339, NOT 0xFFFFFFFF.
 *     (half-word: 0xE339, byte: 0x39 even / 0xE3 odd)
 *   - Flash is mapped at both 0x00000000 and 0x08000000 (mirrored).
 *   - At SYSCLK > 120 MHz, flash access clock defaults to SYSCLK/2
 *     via FLASH_CTLR SCKMOD bit (safe for reads, but be aware).
 *
 * Usage:
 *   #define MY_SETTINGS_PAGE  (flash_end_addr() - FLASH_PAGE_SIZE)
 *   flash_erase_page(MY_SETTINGS_PAGE);
 *   flash_write(MY_SETTINGS_PAGE, (uint8_t *)&cfg, sizeof(cfg));
 *   flash_read(MY_SETTINGS_PAGE, (uint8_t *)&cfg, sizeof(cfg));
 */

#ifndef ROVARI_FLASH_H
#define ROVARI_FLASH_H

#include <stdint.h>

/* ======================================================================
 *  Constants
 * ====================================================================== */

#define FLASH_BASE_ADDR     0x08000000UL
#define FLASH_PAGE_SIZE     4096                 /* CH32V307: 4 KB per page */
#define FLASH_ERASED_WORD   0xE339E339UL         /* WCH quirk: erased != 0xFFFFFFFF */

/*
 * Flash size depends on the linker script partition.
 * Use flash_end_addr() for the actual runtime boundary.
 * These macros assume the default 256K partition for convenience.
 */
#define FLASH_DEFAULT_SIZE  (256 * 1024)
#define FLASH_END_ADDR      (FLASH_BASE_ADDR + FLASH_DEFAULT_SIZE)

/* Convenience: last N pages for user data storage (256K partition, 4K pages) */
#define FLASH_USER_PAGE_1   (FLASH_END_ADDR - FLASH_PAGE_SIZE)          /* 0x0803F000 */
#define FLASH_USER_PAGE_2   (FLASH_END_ADDR - (2 * FLASH_PAGE_SIZE))    /* 0x0803E000 */
#define FLASH_USER_PAGE_3   (FLASH_END_ADDR - (3 * FLASH_PAGE_SIZE))    /* 0x0803D000 */
#define FLASH_USER_PAGE_4   (FLASH_END_ADDR - (4 * FLASH_PAGE_SIZE))    /* 0x0803C000 */

/* ======================================================================
 *  C API
 * ====================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the actual flash end address by reading the chip's flash size register.
 * Works regardless of linker script partition.
 *
 * @return  address one byte past the last flash byte
 */
uint32_t flash_end_addr(void);

/**
 * Erase a single flash page (4096 bytes).
 * The address must be page-aligned (multiple of 4096).
 *
 * @param page_addr  start address of the page (must be 4K-aligned)
 * @return           0 on success, non-zero on error
 */
uint8_t flash_erase_page(uint32_t page_addr);

/**
 * Write data to flash. The destination region must already be erased.
 * Data is written in 16-bit half-words; odd-length buffers are padded
 * with 0xFF for the last byte.
 *
 * @param addr  destination address in flash (must be half-word aligned)
 * @param data  pointer to source data
 * @param len   number of bytes to write
 * @return      0 on success, non-zero on error
 */
uint8_t flash_write(uint32_t addr, const uint8_t *data, uint16_t len);

/**
 * Read data from flash into a RAM buffer.
 * This is a simple memcpy from flash; no unlock required.
 *
 * @param addr  source address in flash
 * @param buf   destination buffer in RAM
 * @param len   number of bytes to read
 */
void flash_read(uint32_t addr, uint8_t *buf, uint16_t len);

/**
 * Write a single 32-bit word to flash. Address must be word-aligned
 * and the target location must already be erased.
 *
 * @param addr  destination address (must be 4-byte aligned)
 * @param value 32-bit value to write
 * @return      0 on success, non-zero on error
 */
uint8_t flash_write_word(uint32_t addr, uint32_t value);

/**
 * Read a single 32-bit word from flash.
 *
 * @param addr  source address (must be 4-byte aligned)
 * @return      32-bit value at that address
 */
uint32_t flash_read_word(uint32_t addr);

/**
 * Check if a flash page is erased.
 * On the CH32V307, erased flash reads as 0xE339E339 (not 0xFFFFFFFF).
 *
 * @param page_addr  start address of the page (4K-aligned)
 * @return           1 if erased, 0 if any word differs from erased pattern
 */
uint8_t flash_is_page_erased(uint32_t page_addr);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_FLASH_H */