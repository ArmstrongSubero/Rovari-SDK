/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_diskio.c
 * @brief FatFs disk-I/O boundary adapter for SD-over-SPI.
 *
 * Bridges the FatFs disk_*() vendor interface (names and signatures fixed
 * by FatFs) to the Rovari SD card driver. This is a SEVS boundary adapter
 * (Section 1.5): it validates parameters and translates results, while the
 * FatFs core and the function-signature contract are vendor-defined.
 * get_fattime() returns real timestamps when the RTC driver is compiled in.
 */

#include <stddef.h>
#include "sevs_runtime.h"
#include "ff.h"
#include "diskio.h"
#include "rovari_sdcard.h"

/* Check if RTC is available at compile time */
#if __has_include("rovari_rtc.h")
#include "rovari_rtc.h"
#define HAS_RTC 1
#else
#define HAS_RTC 0
#endif

#define DISK_SECTOR_SIZE 512

/* -----------------------------------------------------------------------
 *  FatFs required functions (vendor-defined signatures)
 * ----------------------------------------------------------------------- */

/**
 * @brief Report the SD disk status to FatFs.
 * @param[in] pdrv Physical drive number (single drive; ignored).
 * @return 0 if mounted, STA_NOINIT otherwise.
 * @req REQ-ROVARI-DISKIO-0010
 */
DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return sd_is_mounted() ? 0 : STA_NOINIT;
}

/**
 * @brief Initialize the SD disk for FatFs.
 * @param[in] pdrv Physical drive number (ignored).
 * @return 0 on success, STA_NOINIT on failure.
 * @req REQ-ROVARI-DISKIO-0010
 */
DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return sd_disk_initialize() == 0 ? 0 : STA_NOINIT;
}

/**
 * @brief Read sectors for FatFs.
 * @param[in]  pdrv   Physical drive number (ignored).
 * @param[out] buff   Destination buffer (count*512 bytes).
 * @param[in]  sector Start sector (LBA).
 * @param[in]  count  Sector count.
 * @return RES_OK on success, RES_ERROR on failure.
 * @req REQ-ROVARI-DISKIO-0011
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    SEVS_REQUIRE_NOT_NULL(buff);
    return sd_disk_read(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
/**
 * @brief Write sectors for FatFs.
 * @param[in] pdrv   Physical drive number (ignored).
 * @param[in] buff   Source buffer (count*512 bytes).
 * @param[in] sector Start sector (LBA).
 * @param[in] count  Sector count.
 * @return RES_OK on success, RES_ERROR on failure.
 * @req REQ-ROVARI-DISKIO-0011
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    SEVS_REQUIRE_NOT_NULL(buff);
    return sd_disk_write(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}
#endif

/**
 * @brief Service FatFs disk control commands.
 * @param[in]     pdrv Physical drive number (ignored).
 * @param[in]     cmd  Control command.
 * @param[in,out] buff Command-specific buffer.
 * @return RES_OK, or RES_PARERR for unsupported commands.
 * @req REQ-ROVARI-DISKIO-0012
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        SEVS_REQUIRE_NOT_NULL(buff);
        *(WORD *)buff = DISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        SEVS_REQUIRE_NOT_NULL(buff);
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/**
 * @brief Provide the current time to FatFs in its packed format.
 *
 * Returns the RTC date/time when the RTC driver is compiled in, otherwise
 * a fixed 2025-01-01 00:00:00 timestamp.
 *
 * @return FatFs-packed timestamp DWORD.
 * @req REQ-ROVARI-DISKIO-0013
 */
DWORD get_fattime(void)
{
#if HAS_RTC
    RtcDateTime dt;
    rtc_get_datetime(&dt);

    return ((DWORD)(dt.year - 1980) << 25) |
           ((DWORD)dt.month << 21) |
           ((DWORD)dt.day << 16) |
           ((DWORD)dt.hour << 11) |
           ((DWORD)dt.min << 5) |
           ((DWORD)(dt.sec / 2));
#else
    return ((DWORD)(2025 - 1980) << 25) |
           ((DWORD)1 << 21) |
           ((DWORD)1 << 16);
#endif
}
