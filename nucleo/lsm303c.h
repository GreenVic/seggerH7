#pragma once
#include "../system/stm32h7xx.h"

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

void lsm303c_init_la();
uint8_t lsm303c_read_la_status();
void lsm303c_read_la (uint8_t* buf);

void lsm303c_init_mf();
void lsm303c_read_mf (uint8_t *buf);
void lsm303c_read_mf_b (uint8_t *buf);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
