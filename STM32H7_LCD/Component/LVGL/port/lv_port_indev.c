/**
  ******************************************************************************
  * @file    lv_port_indev.c
  * @brief   LVGL 输入移植层：GT911 电容触摸（单点指针设备）
  *
  *  要点：GT911 采用"有没有新数据包"的方式上报，所以
  *    - gt911_read() 返回 1 且 count == 0  -> 手指刚抬起
  *    - gt911_read() 返回 1 且 count  > 0  -> 有新坐标
  *    - gt911_read() 返回 0                -> 状态没变（手指按住不动时也会这样）
  *  因此必须**缓存上一次的状态**，不能在"没新数据"时报抬起，
  *  否则按住不动会被 LVGL 当成反复松手，点击会抖。
  *
  *  ⚠️ 这里是轮询式读取（LVGL 每次刷新时调一次），不是中断触发。
  *     每次读 1 字节状态 + 最多 8 字节坐标，I2C 400kHz 下开销可以忽略。
  *     若以后要省这点开销，可以用 LCD_TP_INT(PI3) 做外部中断，只在有数据时才读。
  ******************************************************************************
  */
#include "lv_port.h"
#include "gt911.h"
#include "lcd.h"

/* 上一次的触摸状态与坐标 */
static lv_indev_state_t s_state = LV_INDEV_STATE_RELEASED;
static lv_point_t       s_point;

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    gt911_touch_t touch;

    LV_UNUSED(indev);

    if (gt911_read(&touch) != 0U)          /* 有新数据包才更新状态 */
    {
        if (touch.count > 0U)
        {
            /* 只用第 1 个点：LVGL 的 POINTER 类型是单点设备 */
            int32_t x = (int32_t)touch.x[0];
            int32_t y = (int32_t)touch.y[0];

            /* 越界保护：个别固件的 GT911 会报出屏外坐标 */
            if (x < 0) { x = 0; }
            if (y < 0) { y = 0; }
            if (x >= (int32_t)LCD_WIDTH)  { x = (int32_t)LCD_WIDTH  - 1; }
            if (y >= (int32_t)LCD_HEIGHT) { y = (int32_t)LCD_HEIGHT - 1; }

            s_point.x = x;
            s_point.y = y;
            s_state   = LV_INDEV_STATE_PRESSED;
        }
        else
        {
            s_state = LV_INDEV_STATE_RELEASED;
        }
    }

    data->point = s_point;
    data->state = s_state;
}

void lv_port_indev_init(void)
{
    lv_indev_t *indev;

    indev = lv_indev_create();
    if (indev == NULL)
    {
        LV_LOG_ERROR("lv_indev_create failed");
        return;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);
}
