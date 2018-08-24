// lsm303.c
#include "lsm303c.h"

static const uint8_t LA_ADDRESS = 0x3A;
static const uint8_t MF_ADDRESS = 0x3c;
//{{{  Linear acceleration related register addesses
static const uint8_t WHO_AM_I_A   = 0x0F;
static const uint8_t ACT_THS_A    = 0x1E;
static const uint8_t ACT_DUR_A    = 0x1F;
static const uint8_t CTRL_REG1_A  = 0x20;
static const uint8_t CTRL_REG2_A  = 0x21;
static const uint8_t CTRL_REG3_A  = 0x22;
static const uint8_t CTRL_REG4_A  = 0x23;
static const uint8_t CTRL_REG5_A  = 0x24;
static const uint8_t CTRL_REG6_A  = 0x25;
static const uint8_t CTRL_REG7_A  = 0x26;
static const uint8_t STATUS_REG_A = 0x27;
static const uint8_t OUT_X_L_A    = 0x28;
static const uint8_t OUT_X_H_A    = 0x29;
static const uint8_t OUT_Y_L_A    = 0x2a;
static const uint8_t OUT_Y_H_A    = 0x2b;
static const uint8_t OUT_Z_L_A    = 0x2c;
static const uint8_t OUT_Z_H_A    = 0x2d;
static const uint8_t FIFO_CTRL    = 0x2e;
static const uint8_t FIFO_SRC     = 0x2f;
static const uint8_t IG_CFG1_A    = 0x30;
static const uint8_t IG_SRC1_A    = 0x31;
static const uint8_t IG_THS_X1_A  = 0x32;
static const uint8_t IG_THS_Y1_A  = 0x33;
static const uint8_t IG_THS_Z1_A  = 0x34;
static const uint8_t IG_DUR1_A    = 0x35;
static const uint8_t IG_CFG2_A    = 0x36;
static const uint8_t IG_SRC2_A    = 0x37;
static const uint8_t IG_THS2_A    = 0x38;
static const uint8_t IG_DUR2_A    = 0x39;
static const uint8_t XL_REFERENCE = 0x3a;
static const uint8_t XH_REFERENCE = 0x3b;
static const uint8_t YL_REFERENCE = 0x3c;
static const uint8_t YH_REFERENCE = 0x3d;
static const uint8_t ZL_REFERENCE = 0x3e;
static const uint8_t ZH_REFERENCE = 0x3f;

// Options for CTRL_REG1_A
static const uint8_t HR        = 0x80;
static const uint8_t ODR_10Hz  = 0x10;
static const uint8_t ODR_50Hz  = 0x20;
static const uint8_t ODR_100Hz = 0x30;
static const uint8_t ODR_200Hz = 0x40;
static const uint8_t ODR_400Hz = 0x50;
static const uint8_t ODR_800Hz = 0x60;
static const uint8_t BDU       = 0x08;
static const uint8_t Zen       = 0x04;
static const uint8_t Yen       = 0x02;
static const uint8_t Xen       = 0x01;

// Options for CTRL_REG4_A
static const uint8_t SIM       = 0x01;
//}}}
//{{{  Magnetic field related register addesses
static const uint8_t CRA_REG_M       = 0x00;
static const uint8_t CRB_REG_M       = 0x01;
static const uint8_t MR_REG_M        = 0x02;
static const uint8_t OUT_X_H_M       = 0x03;
static const uint8_t OUT_X_L_M       = 0x04;
static const uint8_t OUT_Z_H_M       = 0x05;
static const uint8_t OUT_Z_L_M       = 0x06;
static const uint8_t OUT_Y_H_M       = 0x07;
static const uint8_t OUT_Y_L_M       = 0x08;
static const uint8_t SR_REG_M        = 0x09;
static const uint8_t IRA_REG_M       = 0x0a;
static const uint8_t IRB_REG_M       = 0x0b;
static const uint8_t IRC_REG_M       = 0x0c;
static const uint8_t TEMP_OUT_H_M    = 0x31;
static const uint8_t TEMP_OUT_L_M    = 0x32;
//}}}

I2C_HandleTypeDef I2cHandle;

void I2C4_EV_IRQHandler() { HAL_I2C_EV_IRQHandler (&I2cHandle); }
void I2C4_ER_IRQHandler() { HAL_I2C_ER_IRQHandler (&I2cHandle); }

//{{{
void lsm303c_init_la() {

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

  I2cHandle.Instance = I2C4;
  I2cHandle.Init.Timing = 0x20601138;
  I2cHandle.Init.OwnAddress1 = 0;
  I2cHandle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  I2cHandle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  I2cHandle.Init.OwnAddress2 = 0;
  I2cHandle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  I2cHandle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  I2cHandle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init (&I2cHandle) != HAL_OK)
    printf ("HAL_I2C_Init error\n");

  HAL_I2CEx_ConfigAnalogFilter (&I2cHandle, I2C_ANALOGFILTER_ENABLE);

  uint8_t reg = WHO_AM_I_A;
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_init_la id tx error\n");

  uint8_t buf[4] = {0};
  if (HAL_I2C_Master_Receive (&I2cHandle, LA_ADDRESS, buf, 1, 10000) != HAL_OK)
    printf ("lsm303c_init_la id rx error\n");
  else
    printf ("whoami %x\n", buf[0]);

  uint8_t init[2] =  {CTRL_REG1_A, ODR_800Hz | Xen | Yen | Zen };
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, init, 2, 10000)  != HAL_OK)
    printf ("lsm303c_init_la tx ctrl_reg1_a error\n");
  }
//}}}
//{{{
uint8_t lsm303c_read_la_status() {

  uint8_t reg = STATUS_REG_A;
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_read_la_status id tx error\n");

  uint8_t buf[4] = {0};
  if (HAL_I2C_Master_Receive (&I2cHandle, LA_ADDRESS, buf, 1, 10000) != HAL_OK)
    printf ("lsm303c_read_la_status id rx error\n");
  //else
  //  printf ("STATUS_REG_A %x\n", buf[0]);

  return buf[0];
  }
//}}}
//{{{
void lsm303c_read_la (uint8_t* buf) {

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
void lsm303c_init_mf() {

  uint8_t init[2][2] = {
      {CRB_REG_M, (uint8_t)0x20},
      {MR_REG_M, (uint8_t)0x00}
    };

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3c, init[0], 2);
  if (ret != HAL_OK)
    return;

  ret = HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3c, init[1], 2);
  if (ret != HAL_OK)
    return;
  }
//}}}
//{{{
void lsm303c_read_mf (uint8_t *buf) {

  uint8_t reg = OUT_X_H_M;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Sequential_Transmit_IT (&I2cHandle, 0x3c, &reg, 1, I2C_FIRST_FRAME);
  if (ret != HAL_OK)
      return;
  while (!__HAL_I2C_GET_FLAG(&I2cHandle, I2C_FLAG_TC));

  ret = HAL_I2C_Master_Sequential_Receive_IT (&I2cHandle, 0x3c, buf, 6, I2C_LAST_FRAME);
  if (ret != HAL_OK)
    return;
  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
