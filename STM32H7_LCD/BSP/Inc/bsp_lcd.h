/**
  ******************************************************************************
  * @file    bsp_lcd.h
  * @brief   LTDC + DMA2D LCD driver for a 7" 800x480 18-bit RGB666 panel.
  *
  *  Hardware:
  *    Panel      : 7 inch, 800x480, 40-pin FPC, RGB666 (18 data lines)
  *    LTDC       : 22 signals (R2-R7 / G2-G7 / B2-B7 / CLK / DE / HSYNC / VSYNC)
  *    Pixel clock: PLL3R -> 33.333 MHz  (about 60 Hz refresh)
  *    Frame buffer: SDRAM at 0xC0000000, ARGB8888 (4 bytes per pixel, 1.5 MB)
  *    Backlight  : PB0 -> R64(0R) -> U3 MT9284-28J EN (active high)
  *    Touch reset: PG14 (LCD_TP_RST) - SHARED WITH THE PANEL RESET (FPC pin 31)
  *
  *  IMPORTANT - pixel format:
  *    The LTDC layer must be ARGB8888 and the DMA2D ColorMode must be
  *    DMA2D_OUTPUT_ARGB8888.  The two peripherals name the formats
  *    differently, and mixing them swaps colour channels.
  ******************************************************************************
  */
#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ltdc.h"

/* ======================== 屏幕参数（按屏修这里�?======================== */
#define LCD_WIDTH               800U
#define LCD_HEIGHT              480U
#define LCD_BYTES_PER_PIXEL     4U                                  /* RGB888 -> 18bit RGB666 panel */
#define LCD_PIXEL_FORMAT        LTDC_PIXEL_FORMAT_RGB888

/* 显存：放�?SDRAM 开头（MPU Region 2 = 0xC0000000 / 4MB / Non-cacheable�?*/
#define LCD_FB_ADDR             0xC0000000UL
#define LCD_FB_SIZE             (LCD_WIDTH * LCD_HEIGHT * LCD_BYTES_PER_PIXEL)
/* ======================================================================== */

/* ------------------------ 常用颜色（RGB565�?------------------------ */
/* ��ɫ�����: 0xRRGGBB -> ��ǰ���ظ�ʽ */
#if (LCD_PIXEL_FORMAT == LTDC_PIXEL_FORMAT_RGB888)
#define LCD_COLOR(r,g,b)        (0xFF000000UL | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#else
#define LCD_COLOR(r,g,b)        (uint16_t)((((uint32_t)(r) >> 3) << 11) | (((uint32_t)(g) >> 2) << 5) | ((uint32_t)(b) >> 3))
#endif

#define LCD_COLOR_BLACK         LCD_COLOR(0x00U,0x00U,0x00U)
#define LCD_COLOR_WHITE         LCD_COLOR(0xFFU,0xFFU,0xFFU)
#define LCD_COLOR_RED           LCD_COLOR(0xFFU,0x00U,0x00U)
#define LCD_COLOR_GREEN         LCD_COLOR(0x00U,0xFFU,0x00U)
#define LCD_COLOR_BLUE          LCD_COLOR(0x00U,0x00U,0xFFU)
#define LCD_COLOR_YELLOW        LCD_COLOR(0xFFU,0xFFU,0x00U)
#define LCD_COLOR_CYAN          LCD_COLOR(0x00U,0xFFU,0xFFU)
#define LCD_COLOR_MAGENTA       LCD_COLOR(0xFFU,0x00U,0xFFU)
#define LCD_COLOR_GRAY          LCD_COLOR(0x80U,0x80U,0x80U)
#define LCD_COLOR_ORANGE        LCD_COLOR(0xFFU,0x80U,0x00U)

typedef uint32_t lcd_color_t;   /* RGB888 ��ÿ���� 4 �ֽڣ�alpha λ�����ԣ� */

/* ------------------------------ API ------------------------------ */

/* 调试日志钩子（可注入串口输出函数，为 NULL 时静默） */
extern void (*g_lcd_log)(const char *msg);

/* Prepare the display: DMA2D config + clear to black.
   The backlight is NOT turned on here on purpose.

   The GT911 reset pin (PG14) is shared with the panel reset (FPC pin 31), so
   bsp_gt911_init() also resets the panel and blanks the picture for ~20 ms.
   Correct start-up order to avoid a visible flash:

       bsp_lcd_init();          <- display ready, backlight still OFF
       ...draw the first frame...   (invisible so far)
       bsp_gt911_init();        <- panel + touch reset happens here, unseen
       bsp_lcd_backlight(1U);   <- now show everything, no flicker */
void bsp_lcd_init(void);

/* Backlight on/off (PB0 / LCD_BL_EN, active high). Call with 1 after the
   touch controller has been initialised. */
void bsp_lcd_backlight(uint8_t on);

/* 清屏 / 填充矩形（走 DMA2D 硬件加速） */
void bsp_lcd_clear(lcd_color_t color);
void bsp_lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color);

/* 基础绘图（CPU 直接写显存） */
void bsp_lcd_draw_pixel(uint16_t x, uint16_t y, lcd_color_t color);
void bsp_lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, lcd_color_t color);
void bsp_lcd_draw_vline(uint16_t x, uint16_t y, uint16_t h, lcd_color_t color);
void bsp_lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color);
void bsp_lcd_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, lcd_color_t color);

/* 切换 LTDC 图层显存地址（双缓冲用） */
void bsp_lcd_set_framebuffer(uint32_t addr);

/* 八色竖条测试图，用来验证屏和排线 */
void bsp_lcd_test_pattern(void);

/* ------------------------ Text (8x12 ASCII font) ------------------------
   Printable ASCII 0x20..0x7E (space, digits, upper/lower case, punctuation).
   Anything outside that range is drawn as a space.
   x/y is the top-left corner of the first character cell. */
#define LCD_FONT_W   8U
#define LCD_FONT_H   12U

void     bsp_lcd_draw_char(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg);
void     bsp_lcd_draw_text(uint16_t x, uint16_t y, const char *s, lcd_color_t fg, lcd_color_t bg);
uint16_t bsp_lcd_text_width(const char *s);

/* Draw an unsigned integer right-aligned in a fixed number of digits
   (leading zeros), e.g. value 7 with digits=3 -> "007". */
void     bsp_lcd_draw_text_uint(uint16_t x, uint16_t y, uint32_t v, uint8_t digits,
                                lcd_color_t fg, lcd_color_t bg);

/* ------------------------ Frame counter (FPS) ------------------------
   Uses the LTDC line interrupt with line 0 -> exactly one interrupt per
   frame, so the count is the true number of displayed frames.

   bsp_lcd_frame_init()  : enable the interrupt, reset the counter to 0
   bsp_lcd_frame_count() : monotonic frame counter

   Compute the FPS yourself with a 1 second window, e.g.:
       fps = (count_now - count_1s_ago) * 1000 / elapsed_ms; */
void     bsp_lcd_frame_init(void);
uint32_t bsp_lcd_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_LCD_H */
