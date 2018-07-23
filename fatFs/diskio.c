#include "diskio.h"
#include "ff_gen_drv.h"

extern Disk_drvTypeDef  disk;

//{{{
DSTATUS disk_status (BYTE pdrv   /* Physical drive number to identify the drive */ ) {

  DSTATUS stat;
  stat = disk.drv[pdrv]->disk_status(disk.lun[pdrv]);
  return stat;
  }
//}}}
//{{{
/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS disk_initialize (BYTE pdrv       /* Physical drive nmuber to identify the drive */ ) {

  DSTATUS stat = RES_OK;
  if(disk.is_initialized[pdrv] == 0) {
    disk.is_initialized[pdrv] = 1;
    stat = disk.drv[pdrv]->disk_initialize(disk.lun[pdrv]);
    }

  return stat;
  }
//}}}

//{{{
/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT disk_read (BYTE pdrv,    /* Physical drive nmuber to identify the drive */
                   BYTE* buff,   /* Data buffer to store read data */
                   DWORD sector, /* Sector address in LBA */
                   UINT count    /* Number of sectors to read */) {

  return disk.drv[pdrv]->disk_read (disk.lun[pdrv], buff, sector, count);
  }
//}}}
//{{{
/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT disk_write (BYTE pdrv,        /* Physical drive nmuber to identify the drive */
                    const BYTE* buff, /* Data to be written */
                    DWORD sector,     /* Sector address in LBA */
                    UINT count        /* Number of sectors to write */) {

  return disk.drv[pdrv]->disk_write (disk.lun[pdrv], buff, sector, count);
  }
//}}}
//{{{
/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
DRESULT disk_ioctl (BYTE pdrv,  /* Physical drive nmuber (0..) */
                    BYTE cmd,   /* Control code */
                    void* buff  /* Buffer to send/receive control data */) {

  return disk.drv[pdrv]->disk_ioctl (disk.lun[pdrv], cmd, buff);
  }
//}}}

//{{{
DWORD get_fattime()
{
  return 0;
}
//}}}
