/**
  ******************************************************************************
  * @file    bsp_i2c.h
  * @brief   I2C2 统一访问接口（GT911 触摸 + EEPROM 共用一条总线）
  *
  *  硬件：I2C2 = PH4(SCL) / PH5(SDA)，400kHz
  *        总线上挂：
  *          - GT911 电容触摸（寄存器地址 16 位，地址 0x5D 或 0x14）
  *          - JSM24C02 EEPROM（寄存器地址 8 位，地址 0x50）
  ******************************************************************************
  */
#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"

/* ---------------- 总线上的从机地址（均为 7 位，未左移） ---------------- */
#define GT911_I2C_ADDR_5D       0x5DU   /* 复位时 INT 为低 -> 0x5D */
#define GT911_I2C_ADDR_14       0x14U   /* 复位时 INT 为高 -> 0x14 */
#define EEPROM_I2C_ADDR         0x50U   /* JSM24C02 (24C02)        */

#define BSP_I2C_TIMEOUT         100U

/* -------- 8 位寄存器地址（EEPROM 这类器件用） -------- */
HAL_StatusTypeDef bsp_i2c_write8(uint8_t dev_addr, uint8_t reg,
                                 const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef bsp_i2c_read8(uint8_t dev_addr, uint8_t reg,
                                uint8_t *buf, uint16_t len);

/* -------- 16 位寄存器地址（GT911 这类器件用） -------- */
HAL_StatusTypeDef bsp_i2c_write16(uint8_t dev_addr, uint16_t reg,
                                  const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef bsp_i2c_read16(uint8_t dev_addr, uint16_t reg,
                                 uint8_t *buf, uint16_t len);

/* 探测器件是否在线 */
HAL_StatusTypeDef bsp_i2c_probe(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_I2C_H */
