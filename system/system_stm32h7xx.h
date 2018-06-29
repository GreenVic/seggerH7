#pragma once
//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

extern uint32_t SystemCoreClock;             /*!< System Domain1 Clock Frequency  */
extern uint32_t SystemD2Clock;               /*!< System Domain2 Clock Frequency  */
extern const  uint8_t D1CorePrescTable[16] ; /*!< D1CorePrescTable prescalers table values */


extern void SystemInit(void);
extern void SystemCoreClockUpdate(void);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
