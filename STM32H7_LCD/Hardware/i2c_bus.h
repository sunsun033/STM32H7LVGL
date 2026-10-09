/**
  ******************************************************************************
  * @file    i2c_bus.h
  * @brief   I2C2 统一访问接口（GT911 触摸等挂在这条总线上的器件共用）
  *
  *  硬件：I2C2 = PH4(SCL) / PH5(SDA)，400kHz
  *        总线上的从机：
  *          - GT911 电容触摸（寄存器地址 16 位，7 位地址 0x5D 或 0x14）
  *          - JSM24C02 EEPROM（寄存器地址 8 位，7 位地址 0x50）
  *            目前没用到；要用的话按下面 16 位版本照抄一个 8 位的即可
  *            （把 I2C_MEMADD_SIZE_16BIT 改成 I2C_MEMADD_SIZE_8BIT）。
  *
  *  ⚠️ HAL 的 I2C 接口要求传"8 位地址"（7 位地址左移 1 位），
  *     本模块对外统一收 7 位地址，内部左移，避免调用方到处 <<1。
  ******************************************************************************
  */
#ifndef __I2C_BUS_H
#define __I2C_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"

/* 总线上的从机地址（均为 7 位，未左移） */
#define GT911_I2C_ADDR_5D       0x5DU   /* GT911 复位时 INT 为低 -> 0x5D */
#define GT911_I2C_ADDR_14       0x14U   /* GT911 复位时 INT 为高 -> 0x14 */
#define EEPROM_I2C_ADDR         0x50U   /* JSM24C02 (24C02)              */

/* 单次传输超时（ms）。GT911 不会长时间拉死总线，100ms 足够。 */
#define I2C_TIMEOUT             100U

/* 16 位寄存器地址读写（GT911 用） */
HAL_StatusTypeDef i2c_write16(uint8_t dev, uint16_t reg, const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef i2c_read16(uint8_t dev, uint16_t reg, uint8_t *buf, uint16_t len);

/* 探测器件是否在线（返回 HAL_OK 表示有应答）。排查"触摸没反应"时先跑这个 */
HAL_StatusTypeDef i2c_probe(uint8_t dev);

#ifdef __cplusplus
}
#endif
#endif /* __I2C_BUS_H */
