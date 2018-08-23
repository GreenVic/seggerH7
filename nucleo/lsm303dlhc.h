#pragma once
#include "../system/stm32h7xx.h"

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

void lsm303dlhc_init_la();
void lsm303dlhc_read_la (uint8_t* buf);
void lsm303dlhc_read_la_b (uint8_t* buf);

void lsm303dlhc_init_mf();
void lsm303dlhc_read_mf (uint8_t *buf);
void lsm303dlhc_read_mf_b (uint8_t *buf);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
