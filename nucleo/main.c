#include <stdio.h>
#include <stdlib.h>

#include "stm32h7xx_hal.h"

void main() {
  int i;

  for (i = 0; i < 100; i++) {
    printf("Hello World %d!\n", i);
  }
  do {
    i++;
  } while (1);
}
