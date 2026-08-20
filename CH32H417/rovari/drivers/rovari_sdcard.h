/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_sdcard.h - SD Card driver via SDMMC peripheral (CH32H417)
 *
 * Uses the native SDMMC hardware controller for high-speed 4-bit
 * SD card access with DMA. Much faster than the SPI-based driver
 * used on CH32V307.
 *
 * Default pin mapping (SDMMC, no remap):
 *   PC12 (SDCK)  - SD clock        (dedicated, no AF)
 *   PD2  (SDCMD) - SD command      (dedicated, no AF)
 *   PC8  (SDD0)  - SD data 0       (dedicated, no AF)
 *   PC9  (SDD1)  - SD data 1       (AF8)
 *   PC10 (SDD2)  - SD data 2       (AF8)
 *   PC11 (SDD3)  - SD data 3       (AF8)
 *
 * Usage:
 *   SdCard sd;
 *   sd.begin();
 *
 *   FIL fil;
 *   UINT bw;
 *   f_open(&fil, "0:/test.txt", FA_CREATE_ALWAYS | FA_WRITE);
 *   f_write(&fil, "Hello from H417", 15, &bw);
 *   f_close(&fil);
 */

#ifndef ROVARI_SDCARD_H
#define ROVARI_SDCARD_H

#include "rovari_defs.h"
#include "ff.h"
#include <stdint.h>

/* Card type flags (returned by sd_get_card_type) */
#define SD_TYPE_MMC     0x01
#define SD_TYPE_SD1     0x02
#define SD_TYPE_SD2     0x04
#define SD_TYPE_SDC     (SD_TYPE_SD1 | SD_TYPE_SD2)
#define SD_TYPE_BLOCK   0x08

/* Error codes from sd_init() */
#define SD_OK               0
#define SD_ERR_NO_CARD      1
#define SD_ERR_CMD8         2
#define SD_ERR_ACMD41       3
#define SD_ERR_CMD2         4
#define SD_ERR_CMD3         5
#define SD_ERR_CMD9         6
#define SD_ERR_CMD7         7
#define SD_ERR_BUS_WIDTH    8
#define SD_ERR_MOUNT        9

/* ===================================================================
 *  C API
 * =================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize SD card via SDMMC peripheral and mount FatFS.
 * Configures SDMMC pins, performs full card init sequence
 * (CMD0-CMD8-ACMD41-CMD2-CMD3-CMD9-CMD7), switches to 4-bit
 * wide bus, then mounts FatFS drive "0:".
 *
 * @return SD_OK on success, SD_ERR_* on failure
 */
uint8_t sd_init(void);

/**
 * Unmount the SD card filesystem.
 */
void sd_deinit(void);

/**
 * Get the detected card type after init.
 * @return Card type flags (SD_TYPE_SD2 | SD_TYPE_BLOCK for SDHC)
 */
uint8_t sd_get_card_type(void);

/**
 * Check if the SD card is initialized and mounted.
 * @return 1 if ready, 0 if not
 */
uint8_t sd_is_mounted(void);

/**
 * Get card capacity in 512-byte sectors.
 * @return Number of sectors (0 if not initialized)
 */
uint32_t sd_get_sector_count(void);

/* -- FatFS diskio interface (called internally, not by user) -------- */

uint8_t sd_disk_initialize(void);
uint8_t sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count);

#ifdef __cplusplus
}
#endif

/* ===================================================================
 *  C++ API
 * =================================================================== */

#ifdef __cplusplus

class SdCard {
public:
    /** Initialize and mount SD card via SDMMC. */
    uint8_t begin()            { return sd_init(); }

    /** Unmount SD card. */
    void end()                 { sd_deinit(); }

    /** Is the card mounted and ready? */
    uint8_t mounted()          { return sd_is_mounted(); }

    /** Get card type flags. */
    uint8_t cardType()         { return sd_get_card_type(); }

    /** Get card capacity in sectors. */
    uint32_t sectorCount()     { return sd_get_sector_count(); }
};

#endif /* __cplusplus */
#endif /* ROVARI_SDCARD_H */
