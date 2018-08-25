// lsm303c.cpp
#include "lsm303c.h"
//{{{  linear acceleration register addesses
static const uint8_t LA_ADDRESS = 0x3A;

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

// CTRL_REG1_A
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

// CTRL_REG4_A
static const uint8_t BW        = 0xC0;
static const uint8_t FS        = 0x30;
static const uint8_t BW_SCALE  = 0x08;
//}}}
//{{{  magnetic field register addesses
static const uint8_t MF_ADDRESS = 0x3c;

static const uint8_t WHO_AM_I_M   = 0x0F;

static const uint8_t CTRL_REG1_M  = 0x20;
static const uint8_t CTRL_REG2_M  = 0x21;
static const uint8_t CTRL_REG3_M  = 0x22;
static const uint8_t CTRL_REG4_M  = 0x23;
static const uint8_t CTRL_REG5_M  = 0x24;

static const uint8_t STATUS_REG_M = 0x27;

static const uint8_t OUT_X_L_M    = 0x28;
static const uint8_t OUT_X_H_M    = 0x28;
static const uint8_t OUT_Y_L_M    = 0x28;
static const uint8_t OUT_Y_H_M    = 0x28;
static const uint8_t OUT_Z_L_M    = 0x28;
static const uint8_t OUT_Z_H_M    = 0x28;

static const uint8_t TEMP_L_M     = 0x2E;
static const uint8_t TEMP_H_M     = 0x2F;

static const uint8_t INT_CFG_M    = 0x30;
static const uint8_t INT_SRC_M    = 0x31;

static const uint8_t INT_THS_L_M  = 0x32;
static const uint8_t INT_THS_H_M  = 0x33;
//}}}

static I2C_HandleTypeDef I2cHandle;
extern "C" { void I2C4_EV_IRQHandler() { HAL_I2C_EV_IRQHandler (&I2cHandle); } }
extern "C" { void I2C4_ER_IRQHandler() { HAL_I2C_ER_IRQHandler (&I2cHandle); } }

//{{{
void lsm303c_init() {

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_I2C4_CLK_ENABLE();
  //{{{  gpio init
  GPIO_InitTypeDef  GPIO_InitStruct;
  GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
  HAL_GPIO_Init (GPIOD, &GPIO_InitStruct);
  //}}}
  //{{{  i2c init
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
  //}}}
  HAL_NVIC_SetPriority (I2C4_ER_IRQn, 0, 1);
  HAL_NVIC_EnableIRQ (I2C4_ER_IRQn);
  HAL_NVIC_SetPriority (I2C4_EV_IRQn, 0, 2);
  HAL_NVIC_EnableIRQ (I2C4_EV_IRQn);

  HAL_I2CEx_ConfigAnalogFilter (&I2cHandle, I2C_ANALOGFILTER_ENABLE);

  uint8_t reg = WHO_AM_I_A;
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_init id tx error\n");
  if (HAL_I2C_Master_Receive (&I2cHandle, LA_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_init id rx error\n");
  else
    printf ("whoami la %x\n", reg);

  uint8_t laInit[2] =  { CTRL_REG1_A, ODR_400Hz | BDU | Xen | Yen | Zen };
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, laInit, 2, 10000)  != HAL_OK)
    printf ("lsm303c_init tx ctrl_reg1_a error\n");

  reg = WHO_AM_I_M;
  if (HAL_I2C_Master_Transmit (&I2cHandle, MF_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_init tx error\n");
  if (HAL_I2C_Master_Receive (&I2cHandle, MF_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_init rx error\n");
  else
    printf ("whoami mf %x\n", reg);

  uint8_t mfInitReg1[2] = { CTRL_REG1_M, 0x70 }; // ultra high performance, 10hz
  uint8_t mfInitReg2[2] = { CTRL_REG2_M, 0x60 }; // FS += 16gauss
  uint8_t mfInitReg3[2] = { CTRL_REG3_M, 0x00 }; // continuous conversion

  if (HAL_I2C_Master_Transmit (&I2cHandle, MF_ADDRESS, mfInitReg1, 2, 10000)  != HAL_OK)
    printf ("lsm303c_init 1 tx ctrl_reg1_a error\n");
  if (HAL_I2C_Master_Transmit (&I2cHandle, MF_ADDRESS, mfInitReg2, 2, 10000)  != HAL_OK)
    printf ("lsm303c_init 2 tx ctrl_reg1_a error\n");
  if (HAL_I2C_Master_Transmit (&I2cHandle, MF_ADDRESS, mfInitReg3, 2, 10000)  != HAL_OK)
    printf ("lsm303c_init 3 tx ctrl_reg1_a error\n");
  }
//}}}

//{{{
bool lsm303c_read_la_ready() {

  uint8_t reg = STATUS_REG_A;
  if (HAL_I2C_Master_Transmit (&I2cHandle, LA_ADDRESS, &reg, 1, 1000) != HAL_OK)
    printf ("lsm303c_read_la_ready tx error\n");
  if (HAL_I2C_Master_Receive (&I2cHandle, LA_ADDRESS, &reg, 1, 1000) != HAL_OK)
    printf ("lsm303c_read_la_ready rx error\n");

  return reg & 0x07;
  }
//}}}
//{{{
bool lsm303c_read_mf_ready() {

  uint8_t reg = STATUS_REG_M;
  if (HAL_I2C_Master_Transmit (&I2cHandle, MF_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_read_mf_ready tx error\n");
  if (HAL_I2C_Master_Receive (&I2cHandle, MF_ADDRESS, &reg, 1, 10000) != HAL_OK)
    printf ("lsm303c_read_mf_ready rx error\n");

  return reg & 0x07;
  }
//}}}
//{{{
void lsm303c_read_la (int16_t* buf) {

  uint8_t reg = OUT_X_L_A;
  if (HAL_I2C_Master_Sequential_Transmit_IT (&I2cHandle, LA_ADDRESS, &reg, 1, I2C_FIRST_FRAME) != HAL_OK)
    printf ("lsm303c_read_la read error\n");
  while (!__HAL_I2C_GET_FLAG (&I2cHandle, I2C_FLAG_TC));

  if (HAL_I2C_Master_Sequential_Receive_IT (&I2cHandle, LA_ADDRESS, (uint8_t*)buf, 6, I2C_LAST_FRAME) != HAL_OK)
    printf ("lsm303c_read_la read error\n");
  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
//{{{
void lsm303c_read_mf (int16_t* buf) {

  uint8_t reg = OUT_X_L_M;
  if (HAL_I2C_Master_Sequential_Transmit_IT (&I2cHandle, MF_ADDRESS, &reg, 1, I2C_FIRST_FRAME) != HAL_OK) {
    printf ("lsm303c_read_mf tx error\n");
    return;
    }
  while (!__HAL_I2C_GET_FLAG (&I2cHandle, I2C_FLAG_TC));

  if (HAL_I2C_Master_Sequential_Receive_IT (&I2cHandle, MF_ADDRESS, (uint8_t*)buf, 6, I2C_LAST_FRAME) != HAL_OK) {
    printf ("lsm303c_read_mf rx error\n");
    return;
    }
  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
