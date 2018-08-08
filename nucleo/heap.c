#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

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
