/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_diskio_spi.c - FatFS disk I/O glue for SD Card via SPI2
 */

#include "ff.h"
#include "diskio.h"
#include "rovari_sdcard_spi.h"

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return (sd_get_card_type() != 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
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

DWORD get_fattime(void)
{
    /* Fixed timestamp: 2025-01-01 00:00:00 */
    return ((DWORD)(2025 - 1980) << 25) |
           ((DWORD)1 << 21) |
           ((DWORD)1 << 16);
}
