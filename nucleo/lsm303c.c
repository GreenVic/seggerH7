// lsm303.c
#include "lsm303c.h"

static const uint8_t LA_ADDRESS = 0x3A;
static const uint8_t MF_ADDRESS = 0x3c;
//{{{  Linear acceleration related register addesses
static const uint8_t WHO_AM_I_A      = 0x0F;
static const uint8_t CTRL_REG1_A     = 0x20;
static const uint8_t CTRL_REG2_A     = 0x21;
static const uint8_t CTRL_REG3_A     = 0x22;
static const uint8_t CTRL_REG4_A     = 0x23;
static const uint8_t CTRL_REG5_A     = 0x24;
static const uint8_t CTRL_REG6_A     = 0x25;
static const uint8_t REFERENCE_A     = 0x26;
static const uint8_t STATUS_REG_A    = 0x27;
static const uint8_t OUT_X_L_A       = 0x28;
static const uint8_t OUT_X_H_A       = 0x29;
static const uint8_t OUT_Y_L_A       = 0x2a;
static const uint8_t OUT_Y_H_A       = 0x2b;
static const uint8_t OUT_Z_L_A       = 0x2c;
static const uint8_t OUT_Z_H_A       = 0x2d;
static const uint8_t FIFO_CTRL_REG_A = 0x2e;
static const uint8_t FIFO_SRC_REG_A  = 0x2f;
static const uint8_t INT1_CFG_A      = 0x30;
static const uint8_t INT1_SOURCE_A   = 0x31;
static const uint8_t INT1_THS_A      = 0x32;
static const uint8_t INT1_DURATION_A = 0x33;
static const uint8_t INT2_CFG_A      = 0x34;
static const uint8_t INT2_SOURCE_A   = 0x35;
static const uint8_t INT2_THS_A      = 0x36;
static const uint8_t INT2_DURATION_A = 0x37;
static const uint8_t CLICK_CFG_A     = 0x38;
static const uint8_t CLICK_SRC_A     = 0x39;
static const uint8_t CLICK_THS_A     = 0x3a;
static const uint8_t TIME_LIMIT_A    = 0x3b;
static const uint8_t TIME_LATENCY_A  = 0x3c;
static const uint8_t TIME_WINDOW_A   = 0x3d;

// Options for CTRL_REG1_A
static const uint8_t ODR_DOWN  = 0b0000 << 4;
static const uint8_t ODR_1Hz   = 0b0001 << 4;
static const uint8_t ODR_10Hz  = 0b0010 << 4;
static const uint8_t ODR_25Hz  = 0b0011 << 4;
static const uint8_t ODR_50Hz  = 0b0100 << 4;
static const uint8_t ODR_100Hz = 0b0101 << 4;
static const uint8_t ODR_200Hz = 0b0110 << 4;
static const uint8_t ODR_400Hz = 0b0111 << 4;
static const uint8_t LPen      = 0b0001 << 3;
static const uint8_t Zen       = 0b0001 << 2;
static const uint8_t Yen       = 0b0001 << 1;
static const uint8_t Xen       = 0b0001 << 0;

// Options for CTRL_REG4_A
static const uint8_t BDU       = 0b01 << 7;
static const uint8_t BLE       = 0b01 << 6;
static const uint8_t FS_2G     = 0b00 << 4;
static const uint8_t FS_4G     = 0b01 << 4;
static const uint8_t FS_8G     = 0b10 << 4;
static const uint8_t FS_16G    = 0b11 << 4;
static const uint8_t HR        = 0b01 << 3;
static const uint8_t SIM       = 0b01 << 0;
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

void I2C4_EV_IRQHandler() {
  HAL_I2C_EV_IRQHandler (&I2cHandle);
  }
void I2C4_ER_IRQHandler() {
  HAL_I2C_ER_IRQHandler (&I2cHandle);
  }

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

  // I2C TIMING Register define when I2C clock source is APB1 (SYSCLK/4) */
  // I2C TIMING is calculated in case of the I2C Clock source is the APB1CLK = 100 MHz */
  // This example use TIMING to 0x00901954 to reach 400 kHz speed (Rise time = 100 ns, Fall time = 10 ns) */
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
    printf ("HAL_I2C_Init error\n");

  uint8_t reg = WHO_AM_I_A;

  if (HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3A, &reg, 1) != HAL_OK)
    printf ("lsm303c_init_la id tx error\n");

  uint8_t buf = 0;
  if (HAL_I2C_Master_Receive_IT (&I2cHandle, 0x3A, &buf, 1) != HAL_OK)
    printf ("lsm303c_init_la id rx error\n");

  uint8_t init[2][2] = { {CTRL_REG1_A, ODR_400Hz | LPen | Xen | Yen | Zen},
                         {CTRL_REG4_A, BDU | FS_4G | HR} };

  if (HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3A, init[0], 2)  != HAL_OK)
    printf ("lsm303c_init_la tx ctrl_reg1_a error\n");

  if (HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3A, init[1], 2)  != HAL_OK)
    printf ("lsm303c_init_la tx ctrl_reg4_a error\n");
  }
//}}}
//{{{
void lsm303c_read_la (uint8_t* buf) {

  uint8_t reg = OUT_X_L_A | 0b10000000;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Sequential_Transmit_IT (&I2cHandle, 0x3A, &reg, 1, I2C_FIRST_FRAME);
  if (ret != HAL_OK)
    printf ("i2c4 read error\n");

  while (!__HAL_I2C_GET_FLAG(&I2cHandle, I2C_FLAG_TC));

  ret = HAL_I2C_Master_Sequential_Receive_IT (&I2cHandle, 0x3A, buf, 6, I2C_LAST_FRAME);
  if (ret != HAL_OK)
    printf ("i2c4 read error\n");

  while (I2cHandle.State != HAL_I2C_STATE_READY);
  }
//}}}
//{{{
void lsm303c_read_la_b (uint8_t* buf) {

  uint8_t reg = OUT_X_L_A | 0b10000000;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3A, &reg, 1);
  if (ret != HAL_OK)
    printf ("i2c4 readb error\n");

  ret = HAL_I2C_Master_Receive_IT (&I2cHandle, 0x3A, buf, 6);
  if (ret != HAL_OK)
    printf ("i2c4 readb error\n");
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
//{{{
void lsm303c_read_mf_b (uint8_t *buf) {

  uint8_t reg = OUT_X_H_M;

  HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit_IT (&I2cHandle, 0x3c, &reg, 1);
  if (ret != HAL_OK)
    return;


  ret = HAL_I2C_Master_Receive_IT (&I2cHandle, 0x3c, buf, 6);
  if (ret != HAL_OK)
    return;
  }
//}}}
