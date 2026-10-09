/**
  ******************************************************************************
  * @file    lv_port.c
  * @brief   LVGL 移植层：tick 时钟、日志输出、总入口
  ******************************************************************************
  */
#include "lv_port.h"
#include "usart.h"

/* ============================== tick 时钟 ==============================
   LVGL 只需要 1ms 分辨率。
   直接借 HAL 的 SysTick 计数（HAL_IncTick 在 SysTick_Handler 里自增），
   不需要额外占用一个定时器，也不需要改 SysTick 中断。
   ⚠️ 前提是 HAL_Init() 已经调用过（main.c 一开始就调了）。 */
static uint32_t tick_get_cb(void)
{
    return HAL_GetTick();
}

/* ============================== 日志输出 ==============================
   lv_conf.h 里 LV_USE_LOG = 1、LV_LOG_PRINTF = 0，
   所以日志走这里注册的回调，直接打到 USART1（115200-8-N-1）。
   想关掉串口日志：把 lv_conf.h 的 LV_USE_LOG 改成 0 即可，本函数会自然失效。 */
static void log_print_cb(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)lv_strlen(buf), 100U);
}

/* ============================== 总入口 ============================== */

void lv_port_tick_init(void)
{
    lv_tick_set_cb(tick_get_cb);
    lv_log_register_print_cb(log_print_cb);
}

void lv_port_init(void)
{
    lv_init();              /* 必须最先调用：初始化内存池、样式、主题等 */
    lv_port_tick_init();
    lv_port_disp_init();
    lv_port_indev_init();
}
