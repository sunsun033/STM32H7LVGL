/**
  ******************************************************************************
  * @file    bsp_i2c.c
  * @brief   I2C2 统一访问接口实现
  ******************************************************************************
  */
#include "bsp_i2c.h"

/* HAL 的 I2C 接口要求传 8 位地址（7 位地址左移 1 位） */
static inline uint16_t i2c_addr8(uint8_t dev7) 
{
    return (uint16_t)(dev7 << 1);
}

HAL_StatusTypeDef bsp_i2c_write8(uint8_t dev_addr, uint8_t reg,
                                 const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(&hi2c2, i2c_addr8(dev_addr), (uint16_t)reg,
                             I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                             BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef bsp_i2c_read8(uint8_t dev_addr, uint8_t reg,
                                uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c2, i2c_addr8(dev_addr), (uint16_t)reg,
                            I2C_MEMADD_SIZE_8BIT, buf, len,
                            BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef bsp_i2c_write16(uint8_t dev_addr, uint16_t reg,
                                  const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(&hi2c2, i2c_addr8(dev_addr), reg,
                             I2C_MEMADD_SIZE_16BIT, (uint8_t *)buf, len,
                             BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef bsp_i2c_read16(uint8_t dev_addr, uint16_t reg,
                                 uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c2, i2c_addr8(dev_addr), reg,
                            I2C_MEMADD_SIZE_16BIT, buf, len,
                            BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef bsp_i2c_probe(uint8_t dev_addr)
{
    return HAL_I2C_IsDeviceReady(&hi2c2, i2c_addr8(dev_addr), 3U,
                                 BSP_I2C_TIMEOUT);
}
