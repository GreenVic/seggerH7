#include <stdlib.h>

#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

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

size_t xPortGetFreeHeapSize() { return 0; }
size_t xPortGetMinimumEverFreeHeapSize() { return 0; }
