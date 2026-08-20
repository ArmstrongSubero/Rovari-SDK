/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_sdcard.h - SD Card driver over SPI with FatFS integration
 *
 * Provides a simple API for mounting an SD card and accessing files
 * through the FatFS filesystem. The driver handles SPI initialization,
 * card detection, slow/fast clock switching, and the FatFS diskio glue.
 *
 * Wiring (default SPI1):
 *   PA5  (SCK)  -> SD module CLK
 *   PA6  (MISO) -> SD module MISO (DO)
 *   PA7  (MOSI) -> SD module MOSI (DI)
 *   PB8  (GPIO) -> SD module CS
 *
 * Usage:
 *   SdCard sd;
 *   sd.begin();
 *
 *   // FatFS is now available
 *   FIL fil;
 *   f_open(&fil, "0:/test.txt", FA_CREATE_ALWAYS | FA_WRITE);
 *   f_write(&fil, "Hello", 5, &bw);
 *   f_close(&fil);
 */

#ifndef ROVARI_SDCARD_H
#define ROVARI_SDCARD_H

#include "rovari_defs.h"
#include "ff.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 *  Configuration: override in your app before #include "rovari.h"
 * ----------------------------------------------------------------------- */

#ifndef SD_CS_PORT
#define SD_CS_PORT   GPIOB
#endif

#ifndef SD_CS_PIN
#define SD_CS_PIN    GPIO_Pin_8
#endif

/* Card type flags (returned by sd_get_card_type) */
#define SD_TYPE_MMC     0x01
#define SD_TYPE_SD1     0x02
#define SD_TYPE_SD2     0x04
#define SD_TYPE_SDC     (SD_TYPE_SD1 | SD_TYPE_SD2)
#define SD_TYPE_BLOCK   0x08

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize SD card over SPI and mount the filesystem.
 * Configures SPI1 at slow speed for card init, then switches to fast.
 * Calls f_mount() internally.
 * @return 0 on success, 1 if card init failed, 2 if mount failed
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

/* FatFS diskio interface (called by FatFS internals) */
/* These are exposed for diskio.c but users should not call them directly */

uint8_t sd_disk_initialize(void);
uint8_t sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus

class SdCard {
public:
    /**
     * Initialize and mount SD card.
     * @return 0 on success
     */
    uint8_t begin()            { return sd_init(); }

    /** Unmount SD card. */
    void end()                 { sd_deinit(); }

    /** Is the card mounted? */
    uint8_t mounted()          { return sd_is_mounted(); }

    /** Get card type flags. */
    uint8_t cardType()         { return sd_get_card_type(); }
};

#endif /* __cplusplus */
#endif /* ROVARI_SDCARD_H */
