/*
 * diskio.c - FatFs glue for Rovari CH32V003.
 * Thin layer over the proven Part-12 SD driver (sd.c / spi.c): the same
 * SD_Initialize / SD_ReadDisk / SD_WriteDisk that printed "init OK type=6"
 * and mounted on this hardware. No custom init here.
 *
 * Wiring: CS=PC4  SCK=PC5  MOSI=PC6  MISO=PC7.  Board MUST run at 3.3V.
 */

#include "ff.h"
#include "diskio.h"
#include "sd.h"
#include "spi.h"

#define DEV_MMC 0

DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    if (SD_Initialize() == 0) return 0;     /* 0 = success */
    SPI_TransferByte(0xFF);
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (SD_ReadDisk(buff, (uint32_t)sector, (uint8_t)count) == 0) return RES_OK;
    SPI_TransferByte(0xFF);
    return RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (SD_WriteDisk((uint8_t*)buff, (uint32_t)sector, (uint8_t)count) == 0) return RES_OK;
    return RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    DRESULT res = RES_ERROR;
    (void)pdrv;
    switch (cmd) {
        case CTRL_SYNC:
            SD_ChipSelect_Low;
            res = (SD_WaitReady() == 0) ? RES_OK : RES_ERROR;
            SD_ChipSelect_High;
            break;
        case GET_SECTOR_SIZE:  *(WORD*)buff = 512; res = RES_OK; break;
        case GET_BLOCK_SIZE:   *(WORD*)buff = 8;   res = RES_OK; break;
        case GET_SECTOR_COUNT: *(DWORD*)buff = SD_GetSectorCount(); res = RES_OK; break;
        default: res = RES_PARERR; break;
    }
    return res;
}

DWORD get_fattime(void) { return 0; }