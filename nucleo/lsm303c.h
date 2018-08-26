#pragma once
#include "../system/stm32h7xx.h"

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

void lsm303c_init();
bool lsm303c_la_ready();
void lsm303c_la (int16_t* buf);

bool lsm303c_mf_ready();
void lsm303c_mf (int16_t* buf);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
