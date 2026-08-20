/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_psram.h - QSPI PSRAM driver for APS6404L (CH32H417)
 *
 * 8 MB (64 Mbit) SPI/QPI PSRAM on QSPI2, PE10-PE15 (VIO18 1.8V).
 * Self-managed refresh, no erase required, direct read/write.
 *
 * VIO18 MUST remain at 1.8V (default). Do NOT set to 3.3V.
 *
 * Usage:
 *   psram_init();                              // Init QSPI2, reset PSRAM
 *   psram_write(0x000000, data, 256);          // Write 256 bytes at addr 0
 *   psram_read(0x000000, buf, 256);            // Read back
 *   psram_write_quad(0x001000, data, 1024);    // Quad write (4x faster)
 *   psram_read_quad(0x001000, buf, 1024);      // Quad read (4x faster)
 */

#ifndef ROVARI_PSRAM_H
#define ROVARI_PSRAM_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize QSPI2 and the APS6404L PSRAM.
 * Configures PE10-PE15 as QSPI2 AF7, resets the PSRAM,
 * and verifies communication by reading the device ID.
 *
 * @return 0 on success, 1 if communication failed
 */
uint8_t psram_init(void);

/**
 * Read the PSRAM manufacturer ID and KGD status.
 * Returns (MFR_ID << 8) | KGD. Expected: 0x0D5D for good die.
 */
uint32_t psram_read_id(void);

/**
 * Write data to PSRAM using SPI mode (1-line data).
 * No erase needed. Maximum 144 MHz clock.
 *
 * @param addr  24-bit byte address (0x000000 to 0x7FFFFF)
 * @param data  Source buffer
 * @param len   Number of bytes to write
 */
void psram_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * Read data from PSRAM using SPI Fast Read (1-line, 8 dummy cycles).
 *
 * @param addr  24-bit byte address
 * @param data  Destination buffer
 * @param len   Number of bytes to read
 */
void psram_read(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * Write data using Quad mode (4-line data, cmd 0x38).
 * 4x throughput vs SPI write. No erase needed.
 */
void psram_write_quad(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * Read data using Quad mode (4-line addr + data, cmd 0xEB, 6 dummy cycles).
 * 4x throughput vs SPI read. Max 144 MHz.
 */
void psram_read_quad(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * Enter QPI (Quad) mode. After this, all commands use 4-line interface.
 * Use psram_exit_quad_mode() to return to SPI mode.
 */
void psram_enter_quad_mode(void);

/**
 * Exit QPI mode and return to SPI mode.
 */
void psram_exit_quad_mode(void);

/**
 * Enter Halfsleep mode (ultra-low power, data retained).
 * Wake by pulling CS low momentarily, then wait 150 us.
 */
void psram_halfsleep(void);

/**
 * Get PSRAM size in bytes (8388608 = 8 MB).
 */
uint32_t psram_size(void);

/**
 * Write data to PSRAM using DMA (non-blocking FIFO feed).
 * The CPU waits for DMA completion but the FIFO never underruns.
 * Length must be a multiple of 4 bytes.
 *
 * @param addr  24-bit byte address
 * @param data  Source buffer (must be 4-byte aligned)
 * @param len   Number of bytes (must be multiple of 4)
 */
void psram_write_dma(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * Read data from PSRAM using DMA.
 * Length must be a multiple of 4 bytes.
 *
 * @param addr  24-bit byte address
 * @param data  Destination buffer (must be 4-byte aligned)
 * @param len   Number of bytes (must be multiple of 4)
 */
void psram_read_dma(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * Switch VIO18 to 1.8V (MODE1) for PSRAM access.
 * Call before any psram_read/write when sharing VIO18 with SDMMC.
 */
void psram_vio18_select(void);

/**
 * Switch VIO18 to 3.3V (MODE3) for SDMMC/other peripherals.
 * Call after PSRAM operations are complete.
 */
void psram_vio18_release(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_PSRAM_H */
