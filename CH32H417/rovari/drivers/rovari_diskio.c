/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_diskio.c - FatFS disk I/O glue for SD Card via SDMMC (CH32H417)
 *
 * Bridges FatFS disk_*() calls to the Rovari SDMMC SD card driver.
 * If the RTC is available, get_fattime() returns real timestamps.
 */

#include "ff.h"
#include "diskio.h"
#include "rovari_sdcard.h"

/* -- RTC disabled for now: use fixed timestamp ---------------------- */
#define HAS_RTC 0

/* ===================================================================
 *  FatFS required functions
 * =================================================================== */

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    /* Card is usable if it has been identified (card_type != 0) */
    return (sd_get_card_type() != 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    /* If already initialized by sd_init(), don't re-init */
    if (sd_is_mounted() || sd_get_card_type() != 0) return 0;
    return sd_disk_initialize() == 0 ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return sd_disk_read(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return sd_disk_write(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = sd_get_sector_count();
        return (sd_get_sector_count() > 0) ? RES_OK : RES_ERROR;
    default:
        return RES_PARERR;
    }
}

/**
 * Get current time for FatFS timestamps.
 * If the RTC driver is compiled in, returns the real date/time.
 * Otherwise returns a fixed timestamp (2025-01-01 00:00:00).
 *
 * FatFS timestamp format (packed into DWORD):
 *   bits[31:25] = year - 1980
 *   bits[24:21] = month (1-12)
 *   bits[20:16] = day (1-31)
 *   bits[15:11] = hour (0-23)
 *   bits[10:5]  = minute (0-59)
 *   bits[4:0]   = second / 2 (0-29)
 */
DWORD get_fattime(void)
{
#if HAS_RTC
    RtcTime dt = rtc_get();

    return ((DWORD)(dt.year - 1980) << 25) |
           ((DWORD)dt.month << 21) |
           ((DWORD)dt.day << 16) |
           ((DWORD)dt.hour << 11) |
           ((DWORD)dt.min << 5) |
           ((DWORD)(dt.sec / 2));
#else
    /* Fixed timestamp: 2025-01-01 00:00:00 */
    return ((DWORD)(2025 - 1980) << 25) |
           ((DWORD)1 << 21) |
           ((DWORD)1 << 16);
#endif
}
