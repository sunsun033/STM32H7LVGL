/**
  ******************************************************************************
  * @file    lcd.c
  * @brief   屏幕驱动实现（LTDC + DMA2D），7 寸 800x480 18 位 RGB666 面板
  *
  *  本文件只做四件事：
  *    1. 打开 LTDC 换页中断（LVGL 双缓冲切页的垂直消隐同步点）
  *    2. 背光亮度：TIM3_CH3(PB0) 输出 PWM，占空比 = 亮度（0~100）
  *    3. 整屏填充（DMA2D 硬件加速）
  *    4. 把 LTDC 中断交给 HAL 分发
  *
  *  画点/画线/画矩形/写字全部交给 LVGL，不再自己实现。
  ******************************************************************************
  */
#include "lcd.h"
#include "dma2d.h"

/* 两块显存的地址表，lcd_fill() 遍历用 */
static const uint32_t s_fb_addr[LCD_FB_COUNT] =
{
    LCD_FB0_ADDR,
    LCD_FB1_ADDR
};

/* ============================== 背光调光 ==============================
   PB0 -> TIM3_CH3 -> R64(0Ω) -> U3 MT9284-28J 的 EN(CE) 脚。
   寄存器说明和频率选择见 lcd.h 里的"背光调光"注释块。 */

#define LCD_BL_PORT         GPIOB
#define LCD_BL_PIN          GPIO_PIN_0
#define LCD_BL_AF           GPIO_AF2_TIM3       /* PB0 的 TIM3_CH3 复用功能 */
#define LCD_BL_TIM          TIM3

void lcd_backlight_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TypeDef     *tim  = LCD_BL_TIM;

    /* 1) 开时钟：TIM3 在 APB1，PB0 在 GPIOB */
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 2) PB0 切成复用功能。
          MX_GPIO_Init() 把它配成了普通推挽输出，这里必须重新配一次，
          否则 PWM 出不来（引脚被 GPIO 输出占用）。 */
    gpio.Pin       = LCD_BL_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;   /* 2kHz 用最低档足够，还少一点 EMI */
    gpio.Alternate = LCD_BL_AF;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);

    /* 3) 时基：PSC / ARR 的推导在 lcd.h，合起来正好是 LCD_BL_PWM_HZ */
    tim->PSC = (uint32_t)LCD_BL_PSC;
    tim->ARR = (uint32_t)LCD_BL_ARR;
    tim->CR1 = TIM_CR1_ARPE;    /* ARR 预装载 */
    tim->EGR = TIM_EGR_UG;      /* 产生一次更新事件，把 PSC/ARR 立刻载入影子寄存器 */
    tim->SR  = 0U;              /* 清掉 UG 顺带置起来的更新标志，免得后面误判 */

    /* 4) CH3 配成 PWM 模式 1：CNT < CCR3 时输出高电平（EN 高有效）
            110 = PWM 模式 1，OC3PE = 输出比较预装载 */
    tim->CCMR2 = (6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE;
    tim->CCR3  = 0U;            /* 占空比 0 -> 背光灭 */
    tim->CCER  = TIM_CCER_CC3E; /* 使能 CH3 输出，高电平有效 */

    /* 5) 启动计数器 */
    tim->CR1 |= TIM_CR1_CEN;
}

void lcd_backlight_set(uint8_t percent)
{
    uint32_t duty;

    if (percent > LCD_BL_PERCENT_MAX)
    {
        percent = (uint8_t)LCD_BL_PERCENT_MAX;
    }

    /* 百分比 -> 比较值。注意乘的是 (ARR+1) 而不是 ARR：
         0%   -> CCR3 = 0    输出常低 -> MT9284 关断，背光全灭
         100% -> CCR3 = 1000 > ARR -> 输出常高 -> 真正的最亮，
                 而且此时**完全没有开关动作**，最亮档位一点啸叫都没有
       中间值就是线性占空比。 */
    duty = ((uint32_t)percent * (LCD_BL_ARR + 1U)) / 100U;

    LCD_BL_TIM->CCR3 = duty;
}

void lcd_fill(uint32_t color)
{
    uint32_t i;

    /* DMA2D 寄存器到存储器(R2M)模式。
       整屏填充的 OutputOffset 恒为 0，所以每次重新 Init 一次就够；
       局部填充才需要每次把 OOR 寄存器也写上（HAL_DMA2D_Start() 不重载 OOR）。 */
    hdma2d.Init.Mode         = DMA2D_R2M;
    hdma2d.Init.ColorMode    = LCD_DMA2D_OUTPUT_MODE;   /* 必须与 LTDC 层的格式配对 */
    hdma2d.Init.OutputOffset = 0U;
    (void)HAL_DMA2D_Init(&hdma2d);

    for (i = 0U; i < LCD_FB_COUNT; i++)
    {
        (void)HAL_DMA2D_Start(&hdma2d, color, s_fb_addr[i], LCD_WIDTH, LCD_HEIGHT);
        (void)HAL_DMA2D_PollForTransfer(&hdma2d, 1000U);
    }
}

void lcd_init(void)
{
    /* 1) 先把背光 PWM 配好（占空比 0，背光仍然灭着）。
          早点配好可以让"背光关着"这个前提从一上电就成立。 */
    lcd_backlight_init();

    /* 2) 只保留"寄存器重载完成(RR)"这一个中断源。
          HAL_LTDC_Init() 默认打开了三个，另外两个在这里关掉：
            - LI (行中断)   : lv_st_ltdc 用重载中断在垂直消隐期换页，行中断用不到
            - TE (传输错误) : 没有实现回调，开着只是空转
            - FU (FIFO 欠载): 同上。想监控 SDRAM 带宽不够导致的欠载，
                              就保留 FU 并实现 HAL_LTDC_ErrorCallback()。
          顺序很重要：先禁用中断源、再清标志，最后才在 NVIC 里放行；
          反过来的话，之前挂起的标志会让 NVIC 一放行就立刻误进一次中断。 */
    __HAL_LTDC_DISABLE_IT(&hltdc, LTDC_IT_LI | LTDC_IT_TE | LTDC_IT_FU);
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_LI);
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_TE);
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_FU);

    /* 3) 打开 LTDC 全局中断：双缓冲切页的垂直消隐同步靠它 */
    HAL_NVIC_SetPriority(LTDC_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);

    /* 4) 两块显存清成黑色，避免上电时显示 SDRAM 里的随机数据 */
    lcd_fill(LCD_COLOR_BLACK);
}

/* LTDC 全局中断服务函数。
   交给 HAL 分发：其中"重载完成(RRIF)"会回调到 lv_st_ltdc 注册的处理函数，
   那就是 LVGL 切换帧缓冲的同步点（硬件在垂直消隐期完成换页，所以不撕裂）。

   ⚠️ 若以后在 CubeMX 里勾选了 LTDC global interrupt，CubeMX 会在
      Core/Src/stm32h7xx_it.c 里另外生成一个 LTDC_IRQHandler -> 链接报重复符号，
      那时把本函数删掉即可（内容就是下面这一行）。 */
void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}
