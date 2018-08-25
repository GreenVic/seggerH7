#pragma once
#include "../system/stm32h7xx.h"

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

void lsm303c_init_la();
bool lsm303c_read_la_ready();
void lsm303c_read_la (int16_t* buf);

void lsm303c_init_mf();
bool lsm303c_read_mf_ready();
void lsm303c_read_mf (int16_t* buf);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
