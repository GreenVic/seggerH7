#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"

static uint8_t* mSdRamAlloc = (uint8_t*)SDRAM_DEVICE_ADDR;

uint8_t* sdRamAlloc (uint32_t bytes) {

  vTaskSuspendAll();

  uint8_t* alloc = mSdRamAlloc;
  if (alloc + bytes < (uint8_t*)SDRAM_DEVICE_ADDR + SDRAM_DEVICE_SIZE)
    mSdRamAlloc += bytes;
  else
    alloc = NULL;

  xTaskResumeAll();

  return alloc;
  }

void* pvPortMalloc (size_t xWantedSize) {
  vTaskSuspendAll();
  void* pvReturn = malloc (xWantedSize);
  xTaskResumeAll();
  return pvReturn;
  }

void vPortFree (void* pv) {
  if (pv != NULL) {
    vTaskSuspendAll();
    free (pv);
    xTaskResumeAll();
    }
  }
