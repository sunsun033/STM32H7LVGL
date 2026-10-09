/**
  ******************************************************************************
  * @file    lcd.h
  * @brief   屏幕驱动（LTDC + DMA2D），7 寸 800x480 18 位 RGB666 面板
  *
  *  硬件对应：
  *    - 面板    : 7 寸 800x480，40P FPC，RGB666（每组 6 根数据线）
  *    - LTDC    : 22 根信号（R2~R7 / G2~G7 / B2~B7 / CLK / DE / HSYNC / VSYNC）
  *    - 像素时钟: PLL3R -> 33.333MHz（约 60Hz 刷新）
  *    - 显存    : SDRAM 开头，**RGB565（2 字节/像素）**，双缓冲共 1.5MB
  *    - 背光    : PB0 -> R64(0R) -> U3 MT9284-28J 的 EN（高有效）
  *    - 触摸复位: PG14（LCD_TP_RST）—— **与面板复位共用同一根线**（FPC 第 31 脚）
  *
  *  ★ 像素格式：**RGB565**，定义在 Hardware/lcd_format.h（不是 CubeMX 配置）
  *      - LTDC 层格式、DMA2D 输出格式、LVGL 颜色深度三处必须一致，
  *        现在统一由 lcd_format.h 的宏决定，CubeMX 里配错了也会被代码覆盖回来；
  *      - LCD_BPP（每像素字节数）也定义在那里，本文件不再重复定义。
  *
  *  ⚠️ 本文件不属于 CubeMX 生成范围，可自由修改，重新生成代码不会覆盖。
  ******************************************************************************
  */
#ifndef __LCD_H
#define __LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ltdc.h"
#include "sdram.h"
#include "lcd_format.h"     /* ★ 像素格式（LCD_PIXEL_FORMAT / LCD_BPP）的唯一出处 */

/* ============================ 面板参数 ============================ */
#define LCD_WIDTH       800U
#define LCD_HEIGHT      480U
/* LCD_BPP 在 lcd_format.h 里定义（跟着像素格式走）：RGB565=2、ARGB8888=4 */

/* 一个显存缓冲的字节数：800 * 480 * 2 = 768,000(750KB) */
#define LCD_FB_SIZE     (LCD_WIDTH * LCD_HEIGHT * LCD_BPP)

/* 双缓冲显存：紧挨着堆在 SDRAM 最前面，一共 1.5MB。
   交给 LVGL 的 DIRECT(直接渲染) 模式使用，见 Component/LVGL/port/lv_port_disp.c */
#define LCD_FB_COUNT    2U
#define LCD_FB0_ADDR    (SDRAM_BASE)
#define LCD_FB1_ADDR    (SDRAM_BASE + LCD_FB_SIZE)

/* ============================ 颜色 ============================
   ⚠️ 不管显存是 RGB565 还是 ARGB8888，这里的颜色**一律写 0x00RRGGBB**：
   lcd_fill() 把颜色交给 DMA2D，HAL 会按 Init.ColorMode 自己截位
   （RGB565 时取高 5/6/5 位，见 HAL 的 DMA2D_SetConfig）。
   所以以后改像素格式**不用动这些颜色宏**。 */
#define LCD_RGB(r, g, b)    ((uint32_t)(((uint32_t)(r) << 16) | \
                                        ((uint32_t)(g) << 8)  | \
                                         (uint32_t)(b)))

#define LCD_COLOR_BLACK     LCD_RGB(0x00U, 0x00U, 0x00U)
#define LCD_COLOR_WHITE     LCD_RGB(0xFFU, 0xFFU, 0xFFU)
#define LCD_COLOR_RED       LCD_RGB(0xFFU, 0x00U, 0x00U)
#define LCD_COLOR_GREEN     LCD_RGB(0x00U, 0xFFU, 0x00U)
#define LCD_COLOR_BLUE      LCD_RGB(0x00U, 0x00U, 0xFFU)
#define LCD_COLOR_YELLOW    LCD_RGB(0xFFU, 0xFFU, 0x00U)
#define LCD_COLOR_CYAN      LCD_RGB(0x00U, 0xFFU, 0xFFU)
#define LCD_COLOR_MAGENTA   LCD_RGB(0xFFU, 0x00U, 0xFFU)

/* ============================ 背光调光 ============================
   背光链路：PB0 -> R64(0Ω) -> U3 MT9284-28J 的 EN(CE) 脚。

   U3 是升压型白光 LED 驱动（1.2MHz 固定频率，SOT23-6），
   它的 CE 脚既当使能、也当 **PWM 调光输入**（该型号支持 PWM / 模拟两种调光）。
   所以这里用 TIM3_CH3(PB0) 输出 PWM，占空比就是亮度。

   引脚分配：
     PB0 -> TIM3_CH3  (AF2)   <- 本工程用这个
     同一根引脚还备选 TIM1_CH2N (AF1) / TIM8_CH2N (AF3)

   ⚠️ PWM 频率怎么选（改 LCD_BL_PWM_HZ 一个宏就行）：
     MT9284 是**升压**拓扑，EN 关断期间靠输出电容维持 LED 电流，
     频率越高，每个开关周期越来不及重新建立电流调节 -> 低亮度区间会不线性甚至闪。
       200 ~ 500Hz : 线性最好，但可能听到轻微啸叫（陶瓷电容/电感发声）
       2kHz        : 默认值，折中
       10k ~ 20kHz : 听不见，但低亮度可能不线性、闪烁
     有啸叫就调高，低亮度不线性/闪烁就调低。
     可用范围约 100Hz ~ 200kHz。
   ================================================================== */

/* PWM 频率（Hz） */
#define LCD_BL_PWM_HZ       2000U

/* TIM3 计数器时钟：TIM3 挂在 APB1；本工程 HCLK=240MHz、APB1 分频=2
   -> PCLK1=120MHz，定时器时钟再 x2 = 240MHz。
   ⚠️ 如果改了时钟树，这里要跟着改，否则实际 PWM 频率会偏。 */
#define LCD_BL_TIM_CLK_HZ   240000000UL

/* 占空比级数：1000 级，0~100% 每 1% 正好 10 级 */
#define LCD_BL_PWM_STEPS    1000U

#define LCD_BL_ARR          (LCD_BL_PWM_STEPS - 1U)                                     /* 999 */
#define LCD_BL_PSC          ((LCD_BL_TIM_CLK_HZ / (LCD_BL_PWM_HZ * LCD_BL_PWM_STEPS)) - 1U)

/* 亮度范围（和界面上的滑条一致） */
#define LCD_BL_PERCENT_MIN  0U
#define LCD_BL_PERCENT_MAX  100U

/* ============================== API ============================== */

/**
  * @brief  屏幕初始化：配置 DMA2D、打开 LTDC 换页中断、清空两块显存
  * @note   必须在 MX_LTDC_Init() 和 MX_DMA2D_Init() 之后调用。
  *
  *         ⚠️ 这里**故意不开背光**。
  *         触摸控制器 GT911 的复位脚 PG14 和面板复位是同一根线，
  *         gt911_init() 会把面板一起复位（画面黑 ~20ms）。
  *         正确的上电顺序（避免看到闪屏）：
  *
  *             lcd_init();            <- 屏幕就绪，背光仍关着（PWM 占空比 0）
  *             lv_init(); ... 画第一帧 ...   （此时看不见）
  *             gt911_init();          <- 面板+触摸的复位发生在这里，看不见
  *             lcd_backlight_set(100U);  <- 复位动作都做完了再开背光
  */
void lcd_init(void);

/* ------------------------------ 背光 ------------------------------ */

/**
  * @brief  配好背光 PWM：TIM3_CH3 / PB0
  * @note   lcd_init() 里已经调用过，一般不用自己再调。
  *         配好之后占空比是 0，也就是**背光仍然是灭的**，
  *         要亮起来得再调 lcd_backlight_set()。
  */
void lcd_backlight_init(void);

/**
  * @brief  设置背光亮度
  * @param  percent 亮度百分比：
  *                   0   = 全灭（PWM 输出常低，MT9284 进关断，<1µA）
  *                   100 = 最亮（PWM 输出常高，**完全没有开关动作，一点啸叫都没有**）
  *                 中间值就是线性占空比。>100 按 100 处理。
  * @note   采用 1000 级占空比，1% 对应 10 级，滑条从 0 拖到 100 是平滑的。
  */
void lcd_backlight_set(uint8_t percent);

/**
  * @brief  整屏填充（DMA2D 硬件加速，两块显存都填）
  * @param  color 0x00RRGGBB
  * @note   只提供"整屏"填充：整屏的 OutputOffset 恒为 0，就绕开了
  *         "局部填充时 HAL_DMA2D_Start() 不重载 OOR 寄存器导致每行连成一片"
  *         那个坑。任意矩形/图形/文字全部交给 LVGL。
  */
void lcd_fill(uint32_t color);

#ifdef __cplusplus
}
#endif
#endif /* __LCD_H */
