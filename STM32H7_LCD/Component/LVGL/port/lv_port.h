/**
  ******************************************************************************
  * @file    lv_port.h
  * @brief   LVGL 9.3 移植层（显示 + 触摸 + 时钟）
  *
  *  分工：
  *    lv_port.c        系统胶水：tick 时钟、日志输出、（以及总入口 lv_port_init）
  *    lv_port_disp.c   显示：用 LVGL 官方 lv_st_ltdc 驱动接管 LTDC，双缓冲直接渲染
  *    lv_port_indev.c  输入：GT911 电容触摸
  *
  *  典型用法（main.c）：
  *
  *      MX_GPIO_Init(); MX_FMC_Init(); MX_DMA2D_Init(); MX_I2C2_Init();
  *      MX_LTDC_Init(); MX_USART1_UART_Init();
  *
  *      sdram_init();              // 必须先于显存使用；自检要放在 LVGL 之前
  *      lcd_init();                // 屏幕就绪，背光仍关着
  *      lv_port_init();            // LVGL 起来，画第一帧（此时看不见）
  *      gt911_init();              // 面板+触摸复位发生在这里，看不见
  *      lcd_backlight_set(100U);   // 最后才开背光 -> 不闪屏
  *
  *      while (1) { lv_timer_handler(); }
  *
  *  ⚠️ lcd_init() 里打开的 LTDC 中断是双缓冲切页的同步点，别关掉。
  ******************************************************************************
  */
#ifndef __LV_PORT_H
#define __LV_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
  * @brief  LVGL 移植层总初始化：lv_init() + tick + 显示 + 触摸
  * @note   调用前必须先完成 sdram_init() / lcd_init()；
  *         gt911_init() 要放在本函数之后、开背光之前（避免闪屏）。
  */
void lv_port_init(void);

/* ---- 下面三个也可以单独调用，lv_port_init() 内部会依次调用它们 ---- */

/* tick 时钟：直接取 HAL_GetTick()（SysTick，1ms 分辨率），并注册串口日志输出 */
void lv_port_tick_init(void);

/* 显示：创建绑定 LTDC Layer0 的双缓冲直接渲染显示设备 */
void lv_port_disp_init(void);

/* 输入：创建 GT911 触摸指针设备 */
void lv_port_indev_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __LV_PORT_H */
