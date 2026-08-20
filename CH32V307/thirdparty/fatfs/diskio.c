/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs     (C)ChaN, 2019                 */
/* Adapted for CH32V307 SD card over SPI1  - Armstrong Subero            */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    return SD_disk_status(pdrv);
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    return SD_disk_initialize(pdrv);
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    return SD_disk_read(pdrv, buff, sector, count);
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    return SD_disk_write(pdrv, buff, sector, count);
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return SD_disk_ioctl(pdrv, cmd, buff);
}

/* Timestamp stub */
DWORD get_fattime(void)
{
    return 0;
}