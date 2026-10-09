/**
  ******************************************************************************
  * @file    lv_port_disp.c
  * @brief   LVGL 显示移植层：LTDC + 双缓冲直接渲染（不撕裂）
  *
  *  做法：直接使用 LVGL 官方自带的 ST LTDC 驱动 lv_st_ltdc.c
  *        （Component/LVGL/lvgl/src/drivers/display/st_ltdc/）。
  *
  *  为什么这样就够了：
  *    1. lv_st_ltdc_create_direct() 建的是 LV_DISPLAY_RENDER_MODE_DIRECT（直接渲染）
  *       显示设备 —— LVGL 直接渲染进 LTDC 的显存，**不需要任何 memcpy 搬运**。
  *    2. 传两个全屏缓冲进去就是双缓冲：
  *         - LVGL 往"后台"那块渲染；
  *         - 一帧渲染完，flush 回调调用 HAL_LTDC_SetAddress_NoReload() +
  *           HAL_LTDC_Reload(VERTICAL_BLANKING)，让**硬件**在垂直消隐期自动换页；
  *         - 换页完成触发 LTDC 重载中断(RRIF)，驱动收到通知后认为"这一帧送完了"。
  *       所以画面不会撕裂，而且我们不用写任何轮询等待的代码。
  *    3. 双缓冲下两块显存要保持一致，LVGL 自己会把"本帧改动过的区域"同步到另一块
  *       （lv_refr.c 里的 sync_areas），不需要我们操心。
  *
  *  依赖：
  *    - Core/Inc/stm32h7xx_hal_conf.h 里 USE_HAL_LTDC_REGISTER_CALLBACKS = 1U
  *      （重载中断回调要用；CubeMX 里对应 LTDC 的 "Register Callbacks" 勾选）
  *    - lv_conf.h 里 LV_USE_ST_LTDC = 1
  *    - 本文件提供不了的东西它自己拿：ltdc.c 生成的 hltdc 句柄
  ******************************************************************************
  */
#include "lv_port.h"
#include "lv_st_ltdc.h"     /* LVGL 官方 ST LTDC 驱动（该目录已加入头文件搜索路径） */
#include "lcd.h"            /* 会带进 Hardware/lcd_format.h（像素格式唯一定义处） */

/* ==================== 编译期强校验：两边格式必须一致 ====================
   LVGL 的颜色深度（lv_conf.h 的 LV_COLOR_DEPTH）和屏幕像素格式
   （Hardware/lcd_format.h 的 LCD_PIXEL_FORMAT / LCD_LV_COLOR_DEPTH）
   必须对应：16 <-> RGB565、32 <-> ARGB8888。
   这里直接编译报错，免得改了一处忘改另一处，拖到屏幕上才发现串色。
   （本文件同时包含 lvgl.h 和 lcd.h，是唯一能可靠做完这个检查的地方） */
#if (LV_COLOR_DEPTH != LCD_LV_COLOR_DEPTH)
#error "LV_COLOR_DEPTH 与 LCD_PIXEL_FORMAT 不匹配：请让 lv_conf.h 的 LV_COLOR_DEPTH 等于 Hardware/lcd_format.h 的 LCD_LV_COLOR_DEPTH"
#endif

void lv_port_disp_init(void)
{
    lv_display_t *disp;

    /* 把 LTDC 的 Layer0 交给 LVGL，两块显存就是 lcd.h 里定义的双缓冲。
       屏幕分辨率（800x480）和像素格式（RGB565）由 lv_st_ltdc 从
       hltdc.LayerCfg[0] 里读 —— 而 hltdc 的格式已被 ltdc.c 的 USER CODE 区
       强制成 Hardware/lcd_format.h 定义的值，所以三边永远一致。 */
    disp = lv_st_ltdc_create_direct((void *)(uintptr_t)LCD_FB0_ADDR,
                                   (void *)(uintptr_t)LCD_FB1_ADDR,
                                   0U);
    if (disp == NULL)
    {
        /* 只有 lv_display_create() 失败（内存池不够）才会走到这里 */
        LV_LOG_ERROR("lv_st_ltdc_create_direct failed");
        return;
    }

    lv_display_set_default(disp);
}
