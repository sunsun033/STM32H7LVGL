/**
  ******************************************************************************
  * @file    bsp_gt911.h
  * @brief   GT911 电容触摸屏驱动（7 寸 800x480 面板配套）
  *
  *  硬件对应（原理图 SCH_核心板_2026-09-30）：
  *    - SCL : PH4 (I2C2_SCL)
  *    - SDA : PH5 (I2C2_SDA)
  *    - INT : PI3 (LCD_TP_INT)
  *    - RST : PG14 (LCD_TP_RST，与面板复位共用)
  *
  *  ⚠️ GT911 的 I2C 地址由「复位释放瞬间 INT 引脚的电平」决定：
  *       INT = 低 -> 0x5D      INT = 高 -> 0x14
  *     所以初始化时必须先把 INT 切成输出、拉好电平，再释放 RST。
  *     本驱动会两种地址各试一遍。
  ******************************************************************************
  */
#ifndef __BSP_GT911_H
#define __BSP_GT911_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define GT911_MAX_POINTS        5U

/* 触摸结果 */
typedef struct
{
    uint8_t  count;                        /* 本次触摸点数(0~5)      */
    uint16_t x[GT911_MAX_POINTS];          /* 各点 X 坐标(像素)      */
    uint16_t y[GT911_MAX_POINTS];          /* 各点 Y 坐标(像素)      */
} gt911_touch_t;

/**
  * @brief  初始化 GT911：硬件复位 -> 探测 I2C 地址 -> 校验产品 ID
  * @retval 0 = 成功，1 = 失败（未找到器件）
  */
uint8_t bsp_gt911_init(void);

/**
  * @brief  读取一次触摸状态
  * @param  touch 输出参数
  * @retval 1 = 有触摸，0 = 无触摸/读取失败
  */
uint8_t bsp_gt911_scan(gt911_touch_t *touch);

/* 获取探测到的 GT911 7 位 I2C 地址（0 表示未初始化成功） */
uint8_t bsp_gt911_get_addr(void);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_GT911_H */
