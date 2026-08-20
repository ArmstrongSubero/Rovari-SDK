/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_sdcard_spi.h - SD Card driver over SPI2 with FatFS (CH32H417)
 *
 * Uses SPI2 on VDDIO (3.3V) domain pins to avoid VIO18 conflict with PSRAM.
 *   PA12 = SPI2_SCK  (AF5)
 *   PC1  = SPI2_MOSI (AF5)
 *   PC2  = SPI2_MISO (AF5)
 *   PC3  = SD_CS     (GPIO output)
 *
 * Usage:
 *   sd_init();
 *   FIL fil;
 *   f_open(&fil, "0:/test.txt", FA_READ);
 *   f_read(&fil, buf, sizeof(buf), &br);
 *   f_close(&fil);
 */

#ifndef ROVARI_SDCARD_SPI_H
#define ROVARI_SDCARD_SPI_H

#include "rovari_defs.h"
#include "ff.h"
#include <stdint.h>

/* CS pin - override before including if needed */
#ifndef SD_CS_PORT
#define SD_CS_PORT   GPIOC
#endif

#ifndef SD_CS_PIN
#define SD_CS_PIN    GPIO_Pin_3
#endif

/* Card type flags */
#define SD_TYPE_MMC     0x01
#define SD_TYPE_SD1     0x02
#define SD_TYPE_SD2     0x04
#define SD_TYPE_SDC     (SD_TYPE_SD1 | SD_TYPE_SD2)
#define SD_TYPE_BLOCK   0x08

#define SD_OK           0

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  sd_init(void);
void     sd_deinit(void);
uint8_t  sd_get_card_type(void);
uint8_t  sd_is_mounted(void);
uint32_t sd_get_sector_count(void);
uint8_t  sd_disk_initialize(void);
uint8_t  sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t  sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class SdCard {
public:
    uint8_t begin()    { return sd_init(); }
    void end()         { sd_deinit(); }
    uint8_t mounted()  { return sd_is_mounted(); }
    uint8_t cardType() { return sd_get_card_type(); }
};
#endif

#endif /* ROVARI_SDCARD_SPI_H */
