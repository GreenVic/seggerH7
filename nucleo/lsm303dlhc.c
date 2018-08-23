// lsm303.c
#include "lsm303dlhc.h"

I2C_HandleTypeDef I2cHandle;

void I2C4_EV_IRQHandler() { 
  HAL_I2C_EV_IRQHandler (&I2cHandle); 
  }
void I2C4_ER_IRQHandler() { 
  HAL_I2C_ER_IRQHandler (&I2cHandle); 
  }

//{{{
void lsm303dlhc_init_la() {

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_I2C4_CLK_ENABLE();

  GPIO_InitTypeDef  GPIO_InitStruct;
  GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
  HAL_GPIO_Init (GPIOD, &GPIO_InitStruct);

  HAL_NVIC_SetPriority (I2C4_ER_IRQn, 0, 1);
  HAL_NVIC_EnableIRQ (I2C4_ER_IRQn);
  HAL_NVIC_SetPriority (I2C4_EV_IRQn, 0, 2);
  HAL_NVIC_EnableIRQ (I2C4_EV_IRQn);

  I2cHandle.Instance             = I2C4;
  I2cHandle.Init.Timing          = 0x00901954;
  I2cHandle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  I2cHandle.Init.OwnAddress1     = 0x00;
  I2cHandle.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  I2cHandle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  I2cHandle.Init.OwnAddress2     = 0x00;
  I2cHandle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  I2cHandle.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init (&I2cHandle) != HAL_OK)
    printf ("i2c4 init error\n");

  //HAL_I2CEx_ConfigAnalogFilter (&I2cHandle, I2C_ANALOGFILTER_ENABLE);

  uint8_t init[2][2] = { {CTRL_REG1_A, ODR_400Hz | LPen | Xen | Yen | Zen},
                         {CTRL_REG4_A, BDU | FS_4G | HR}
                       };

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, init[0], 2, 1000);
  if (ret != HAL_OK)
    printf ("i2c4 init1 error\n");

  ret = HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, init[1], 2, 1000);
  if (ret != HAL_OK)
    printf ("i2c4 init2 error\n");
  }
//}}}
//{{{
void lsm303dlhc_read_la (uint8_t* buf) {

  uint8_t reg = OUT_X_L_A | 0b10000000;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Sequential_Transmit_IT (&I2cHandle, LA_ADDRESS, &reg, 1, I2C_FIRST_FRAME);
  if (ret != HAL_OK)
    printf ("i2c4 read error\n");

  while (!__HAL_I2C_GET_FLAG(&I2cHandle, I2C_FLAG_TC));

  ret = HAL_I2C_Master_Sequential_Receive_IT (&I2cHandle, LA_ADDRESS, buf, 6, I2C_LAST_FRAME);
  if (ret != HAL_OK)
    printf ("i2c4 read error\n");

  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
//{{{
void lsm303dlhc_read_la_b (uint8_t* buf) {

  uint8_t reg = OUT_X_L_A | 0b10000000;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(&I2cHandle, LA_ADDRESS, &reg, 1, 1000);
  if (ret != HAL_OK)
    printf ("i2c4 readb error\n");

  ret = HAL_I2C_Master_Receive(&I2cHandle, LA_ADDRESS, buf, 6, 1000);
  if (ret != HAL_OK)
    printf ("i2c4 readb error\n");
  }
//}}}

//{{{
void lsm303dlhc_init_mf() {

  uint8_t init[2][2] = {
      {CRB_REG_M, (uint8_t)0x20},
      {MR_REG_M, (uint8_t)0x00}
    };

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(&I2cHandle, MF_ADDRESS, init[0], 2, 1000);
  if (ret != HAL_OK)
    return;

  ret = HAL_I2C_Master_Transmit(&I2cHandle, MF_ADDRESS, init[1], 2, 1000);
  if (ret != HAL_OK)
    return;
  }
//}}}
//{{{
void lsm303dlhc_read_mf (uint8_t *buf) {

  uint8_t reg = OUT_X_H_M;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Sequential_Transmit_IT(&I2cHandle, MF_ADDRESS, &reg, 1, I2C_FIRST_FRAME);
  if (ret != HAL_OK)
      return;
  while (!__HAL_I2C_GET_FLAG(&I2cHandle, I2C_FLAG_TC));

  ret = HAL_I2C_Master_Sequential_Receive_IT(&I2cHandle, MF_ADDRESS, buf, 6, I2C_LAST_FRAME);
  if (ret != HAL_OK)
    return;
  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
//{{{
void lsm303dlhc_read_mf_b (uint8_t *buf) {

  uint8_t reg = OUT_X_H_M;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(&I2cHandle, MF_ADDRESS, &reg, 1, 1000);
  if (ret != HAL_OK)
    return;


  ret = HAL_I2C_Master_Receive(&I2cHandle, MF_ADDRESS, buf, 6, 1000);
  if (ret != HAL_OK)
    return;
  }
//}}}
