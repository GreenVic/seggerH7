#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"

// 0x20000000 0x00020000  sram DTCM 128k
// ...
// 0x24000000 0x00080000  sram axi  512k
// ...
// 0x30000000 0x00020000  sram1  128k
// 0x30020000 0x00020000  sram2  128k
// 0x30040000 0x00008000  sram3  32k
// ....
// 0x30080000 0x00010000  sram4  64k

#define DTCM_ADDR 0x20000000
#define DTCM_SIZE 0x00020000
static uint8_t* mDtcmAlloc = (uint8_t*)DTCM_ADDR;
//{{{
uint8_t* dtcmAlloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mDtcmAlloc;
  if (alloc + bytes <= (uint8_t*)DTCM_ADDR + DTCM_SIZE)
    mDtcmAlloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

#define SRAM123_ADDR 0x30000000
#define SRAM123_SIZE 0x00048000
static uint8_t* mSram123Alloc = (uint8_t*)SRAM123_ADDR;
//{{{
uint8_t* sram123Alloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mSram123Alloc;
  if (alloc + bytes <= (uint8_t*)SRAM123_ADDR + SRAM123_SIZE)
    mSram123Alloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}

//{{{
void* pvPortMalloc (size_t xWantedSize) {
  vTaskSuspendAll();
  void* pvReturn = malloc (xWantedSize);
  xTaskResumeAll();
  return pvReturn;
  }
//}}}
//{{{
void vPortFree (void* pv) {
  if (pv != NULL) {
    vTaskSuspendAll();
    free (pv);
    xTaskResumeAll();
    }
  }
//}}}

static uint8_t* mSdRamAlloc = (uint8_t*)SDRAM_DEVICE_ADDR;
//{{{
uint8_t* sdRamAlloc (size_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mSdRamAlloc;
  if (alloc + bytes <= (uint8_t*)SDRAM_DEVICE_ADDR + SDRAM_DEVICE_SIZE)
    mSdRamAlloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }
//}}}
