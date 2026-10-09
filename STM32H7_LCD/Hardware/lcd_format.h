/**
  ******************************************************************************
  * @file    lcd_format.h
  * @brief   屏幕像素格式的**唯一定义处**（LTDC / DMA2D / LVGL 三处共用）
  *
  *  ★ 为什么要单独开一个文件：
  *    「LTDC 层格式」「DMA2D 输出格式」「LVGL 颜色深度」在 ST 的代码里命名
  *    各不相同，但必须严格一致，少改一处就串色 / 花屏（旧笔记 Bug 6）。
  *    而 Core/Src/ltdc.c、Core/Src/dma2d.c 是 CubeMX 生成的 ——
  *    在 CubeMX 界面里配的格式会随着每次"重新生成代码"把源码改回去。
  *
  *    所以这里把格式**定义在代码里**（本文件不属于 CubeMX 生成范围），
  *    生成的 ltdc.c / dma2d.c 只在各自的 USER CODE 区里读这些宏来强制覆盖：
  *      CubeMX 里配成 ARGB8888 也没关系，代码在初始化末尾会改回这里定义的值。
  *
  *  ★ 本工程用 RGB565（不是 ARGB8888），理由：
  *    - UI/ 里的图片（背景图、图标）本来就是 RGB565，
  *      显存也是 565 时贴图是"原生直拷"，不再逐像素做 565->8888 转换；
  *    - 显存 750KB/块（ARGB8888 要 1.5MB），LTDC 扫描带宽从 133MB/s 降到 66.7MB/s。
  *
  *    代价：面板是 RGB666，LTDC 在 RGB565 模式下把 5/6/5 位送到引脚**高位**
  *    （R7..R3 / G7..G2 / B7..B3），而板上接的是 R2~R7 / G2~G7 / B2~B7，
  *    于是 R2、B2 恒为 0，红蓝各降 1 位精度（照片渐变可能轻微分层），
  *    绿色 6 位全用上、零损失。
  *
  *  ⚠️ 改格式只改下面"唯一开关"那一块，**不要**去动 CubeMX 的配置：
  *      代码会在 MX_LTDC_Init() / MX_DMA2D_Init() 末尾把它覆盖回来。
  ******************************************************************************
  */
#ifndef __LCD_FORMAT_H
#define __LCD_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"       /* 取 LTDC_PIXEL_FORMAT_* / DMA2D_OUTPUT_* 这些 HAL 宏 */

/* ==================== ★ 唯一开关（改格式只改这 5 行） ==================== */
#define LCD_PIXEL_FORMAT        LTDC_PIXEL_FORMAT_RGB565   /* LTDC Layer0 像素格式 */
#define LCD_BPP                 2U                         /* 每像素字节数         */
#define LCD_DMA2D_OUTPUT_MODE   DMA2D_OUTPUT_RGB565        /* DMA2D 输出格式       */
#define LCD_DMA2D_INPUT_MODE    DMA2D_INPUT_RGB565         /* DMA2D 输入格式（M2M）*/
#define LCD_LV_COLOR_DEPTH      16                         /* lv_conf.h 必须一致   */

/* ==================== 编译期自检（改错一个立刻编译不过） ==================== */
#if (LCD_PIXEL_FORMAT == LTDC_PIXEL_FORMAT_RGB565) && (LCD_BPP != 2U)
#error "LCD_PIXEL_FORMAT = RGB565 时 LCD_BPP 必须是 2"
#endif
#if (LCD_PIXEL_FORMAT == LTDC_PIXEL_FORMAT_ARGB8888) && (LCD_BPP != 4U)
#error "LCD_PIXEL_FORMAT = ARGB8888 时 LCD_BPP 必须是 4"
#endif

/* LV_COLOR_DEPTH 与像素格式的对应：16 <-> RGB565、32 <-> ARGB8888。
   这里只在"已经包含过 lvgl.h"时才检查；无条件的强制检查在同目录上层的
   Component/LVGL/port/lv_port_disp.c 里（那里一定已经包含 lvgl.h）。 */
#if defined(LV_VERSION_MAJOR) && (LV_COLOR_DEPTH != LCD_LV_COLOR_DEPTH)
#error "LV_COLOR_DEPTH 与 LCD_PIXEL_FORMAT 不匹配：lv_conf.h 的 LV_COLOR_DEPTH 必须等于 LCD_LV_COLOR_DEPTH"
#endif

#ifdef __cplusplus
}
#endif
#endif /* __LCD_FORMAT_H */
