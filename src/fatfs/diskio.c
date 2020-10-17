// Copyright 2020, Ryan Wendland, usb64
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include "ff.h"
#include "diskio.h"
#include "diskio_wrapper.h"

//Read sector first then compare to see if sector has actually changed
//Can reduce flash wear and actually speed up larger writes as only small parts
//of the file might change.
#define READBEFOREWRITE

static uint32_t sector_size = 0;

DSTATUS disk_status(
    BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
}

DSTATUS disk_initialize(
    BYTE pdrv /* Physical drive number to identify the drive */
)
{
    _disk_init();
    sector_size = _disk_volume_get_block_size();
    return RES_OK;
}

DRESULT disk_read(
    BYTE pdrv,    /* Physical drive nmuber to identify the drive */
    BYTE *buff,   /* Data buffer to store read data */
    LBA_t sector, /* Start sector in LBA */
    UINT count    /* Number of sectors to read */
)
{
    if (sector_size == 0)
        return RES_NOTRDY;

    _read_sector(buff, sector, count);
    return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(
    BYTE pdrv,        /* Physical drive nmuber to identify the drive */
    const BYTE *buff, /* Data to be written */
    LBA_t sector,     /* Start sector in LBA */
    UINT count        /* Number of sectors to write */
)
{
    if (sector_size == 0)
        return RES_NOTRDY;

    _write_sector(buff, sector, count);
    return RES_OK;
}
#endif

DRESULT disk_ioctl(
    BYTE pdrv, /* Physical drive nmuber (0..) */
    BYTE cmd,  /* Control code */
    void *buff /* Buffer to send/receive control data */
)
{
    if (sector_size == 0)
        return RES_NOTRDY;

    switch (cmd)
    {
    case GET_SECTOR_SIZE:
        *(WORD *)buff = _disk_volume_get_block_size();
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = _disk_volume_get_cluster_size();
        break;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = _disk_volume_num_blocks();
        break;
    default:
        break;
    }
    return RES_OK;
}
