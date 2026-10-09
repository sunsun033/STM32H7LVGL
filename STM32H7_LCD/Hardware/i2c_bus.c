/**
  ******************************************************************************
  * @file    i2c_bus.c
  * @brief   I2C2 统一访问接口实现
  ******************************************************************************
  */
#include "i2c_bus.h"

/* HAL 要求 8 位地址（7 位地址左移 1 位），对外统一收 7 位 */
static uint16_t addr8(uint8_t dev)
{
    return (uint16_t)((uint16_t)dev << 1);
}

HAL_StatusTypeDef i2c_write16(uint8_t dev, uint16_t reg, const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(&hi2c2, addr8(dev), reg, I2C_MEMADD_SIZE_16BIT,
                             (uint8_t *)buf, len, I2C_TIMEOUT);
}

HAL_StatusTypeDef i2c_read16(uint8_t dev, uint16_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c2, addr8(dev), reg, I2C_MEMADD_SIZE_16BIT,
                            buf, len, I2C_TIMEOUT);
}

HAL_StatusTypeDef i2c_probe(uint8_t dev)
{
    return HAL_I2C_IsDeviceReady(&hi2c2, addr8(dev), 3U, I2C_TIMEOUT);
}
