#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "heap.h"

static uint8_t* mSdRamAlloc = (uint8_t*)SDRAM_DEVICE_ADDR;

uint8_t* sdRamAlloc (uint32_t bytes) {
	uint8_t* alloc = mSdRamAlloc;
	mSdRamAlloc += bytes;
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
