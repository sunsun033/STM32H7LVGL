/**
  ******************************************************************************
  * @file    gt911.h
  * @brief   GT911 电容触摸屏驱动（7 寸 800x480 面板配套）
  *
  *  硬件对应：
  *    - SCL : PH4 (I2C2_SCL)
  *    - SDA : PH5 (I2C2_SDA)
  *    - INT : PI3 (LCD_TP_INT)
  *    - RST : PG14(LCD_TP_RST) —— **与面板复位共用同一根线**（FPC 第 31 脚）
  *
  *  ★ GT911 的 I2C 地址由"复位释放瞬间 INT 引脚的电平"决定：
  *        INT = 低 -> 0x5D        INT = 高 -> 0x14
  *    所以初始化时必须先把 INT 切成输出、拉好电平，再释放 RST。
  *    本驱动两种地址各试一遍（实测本板为 0x14）。
  *
  *  ⚠️ 因为 RST 与面板复位共用，gt911_init() 会顺带把面板一起复位，
  *     画面会黑约 20ms —— 所以必须放在开背光**之前**调用，
  *     否则用户会看到屏幕闪一下（详见 BSP/LCD_移植与诊断笔记.md 第 3 章 Bug 7）。
  *
  *  ⚠️ 本文件不属于 CubeMX 生成范围，可自由修改，重新生成代码不会覆盖。
  ******************************************************************************
  */
#ifndef __GT911_H
#define __GT911_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define GT911_MAX_POINTS        5U      /* GT911 最多支持 5 点同时触摸 */

/* 一次触摸读取的结果 */
typedef struct
{
    uint8_t  count;                             /* 触摸点数 0~5，0 表示手指已抬起 */
    uint16_t x[GT911_MAX_POINTS];               /* 各点 X 坐标（像素） */
    uint16_t y[GT911_MAX_POINTS];               /* 各点 Y 坐标（像素） */
} gt911_touch_t;

/**
  * @brief  初始化 GT911：硬件复位 -> 探测 I2C 地址 -> 校验产品 ID
  * @retval 0 = 成功，1 = 失败（总线上没找到 GT911）
  * @note   必须在 lcd_init() 之后、lcd_backlight_set() 之前调用。
  */
uint8_t gt911_init(void);

/**
  * @brief  读取一次触摸数据
  * @param  touch 输出参数（不能为 NULL）
  * @retval 1 = 读到了**新的数据包**（此时 touch->count 可能是 0，表示刚抬起）
  *         0 = 没有新数据 / 读取失败
  * @note   ⚠️ 返回 1 且 count = 0 是"抬起"事件，调用方要把它当作松手处理；
  *         返回 0 时表示状态没变，应沿用上一次的坐标和按下状态。
  *         LVGL 的输入设备回调就是按这个语义写的，见 port/lv_port_indev.c。
  */
uint8_t gt911_read(gt911_touch_t *touch);

/* 获取探测到的 GT911 7 位 I2C 地址（0 表示还没初始化成功） */
uint8_t gt911_get_addr(void);

#ifdef __cplusplus
}
#endif
#endif /* __GT911_H */
