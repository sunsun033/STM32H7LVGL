/**
  ******************************************************************************
  * @file    gt911.c
  * @brief   GT911 电容触摸屏驱动实现
  ******************************************************************************
  */
#include "gt911.h"
#include "i2c_bus.h"

/* ============================ GT911 寄存器 ============================ */
#define GT911_REG_PRODUCT_ID    0x8140U     /* 4 字节："911" + 版本号      */
#define GT911_REG_STATUS        0x814EU     /* bit7=有新数据, bit0~3=点数  */
#define GT911_REG_POINT1        0x814FU     /* 第 1 个点，每个点 8 字节    */

#define GT911_POINT_SIZE        8U

/* 当前使用的 I2C 地址（0 = 还没探测到） */
static uint8_t s_addr = 0U;

/* ========================= INT 引脚方向切换 =========================
   INT 平时是高阻输入，只有复位选地址时才临时切成输出。 */
static void int_as_output(void)
{
    GPIO_InitTypeDef cfg = {0};

    cfg.Pin   = LCD_TP_INT_Pin;
    cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    cfg.Pull  = GPIO_NOPULL;
    cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_TP_INT_GPIO_Port, &cfg);
}

static void int_as_input(void)
{
    GPIO_InitTypeDef cfg = {0};

    cfg.Pin   = LCD_TP_INT_Pin;
    cfg.Mode  = GPIO_MODE_INPUT;
    cfg.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(LCD_TP_INT_GPIO_Port, &cfg);
}

/* ============================ 复位时序 ============================
   按 GT911 数据手册：RST 上升沿锁存 INT 电平来决定 I2C 地址。
   int_high: 0 -> INT 拉低(地址 0x5D)，1 -> INT 拉高(地址 0x14)。 */
static void hw_reset(uint8_t int_high)
{
    GPIO_PinState level = (int_high != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    /* 1) INT 先切成输出并给出目标电平 */
    int_as_output();
    HAL_GPIO_WritePin(LCD_TP_INT_GPIO_Port, LCD_TP_INT_Pin, level);
    HAL_Delay(10U);

    /* 2) RST 拉低，保持 >=10ms */
    HAL_GPIO_WritePin(LCD_TP_RST_GPIO_Port, LCD_TP_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);

    /* 3) RST 上升沿会被锁存，再确认一次 INT 电平 */
    HAL_GPIO_WritePin(LCD_TP_INT_GPIO_Port, LCD_TP_INT_Pin, level);
    HAL_Delay(1U);

    /* 4) RST 拉高，地址在这一刻被锁存 */
    HAL_GPIO_WritePin(LCD_TP_RST_GPIO_Port, LCD_TP_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(2U);

    /* 5) INT 及时释放为高阻输入 */
    int_as_input();

    /* 6) 等 GT911 内部初始化完成（手册要求 >=55ms，这里留足余量） */
    HAL_Delay(300U);
}

/* ============================ 对外接口 ============================ */

uint8_t gt911_get_addr(void)
{
    return s_addr;
}

uint8_t gt911_init(void)
{
    /* 两种地址各试一遍：先用 INT 电平复位，再读产品 ID 校验 */
    static const uint8_t addr_list[2]  = { GT911_I2C_ADDR_5D, GT911_I2C_ADDR_14 };
    static const uint8_t level_list[2] = { 0U, 1U };
    uint8_t id[4];
    uint8_t i;
    uint8_t retry;

    s_addr = 0U;

    for (i = 0U; i < 2U; i++)
    {
        hw_reset(level_list[i]);

        for (retry = 0U; retry < 3U; retry++)
        {
            if (i2c_read16(addr_list[i], GT911_REG_PRODUCT_ID, id, 4U) == HAL_OK)
            {
                /* 产品 ID 前 3 字节应该是 ASCII "911" */
                if ((id[0] == (uint8_t)'9') &&
                    (id[1] == (uint8_t)'1') &&
                    (id[2] == (uint8_t)'1'))
                {
                    s_addr = addr_list[i];
                    return 0U;
                }
            }
            HAL_Delay(20U);
        }
    }

    return 1U;      /* 总线上没找到 GT911 */
}

uint8_t gt911_read(gt911_touch_t *touch)
{
    uint8_t status = 0U;
    uint8_t buf[GT911_POINT_SIZE];
    uint8_t count;
    uint8_t i;
    uint8_t clear = 0U;

    if ((touch == NULL) || (s_addr == 0U))
    {
        return 0U;
    }

    /* 1) 读状态寄存器：bit7 = 1 说明有新数据 */
    if (i2c_read16(s_addr, GT911_REG_STATUS, &status, 1U) != HAL_OK)
    {
        return 0U;
    }
    if ((status & 0x80U) == 0U)
    {
        return 0U;                      /* 没有新数据，调用方沿用上次状态 */
    }

    count = (uint8_t)(status & 0x0FU);  /* bit0~3 = 触摸点数 */
    if (count > GT911_MAX_POINTS)
    {
        count = GT911_MAX_POINTS;
    }

    /* 2) 逐点读坐标：X / Y 都是低字节在前 */
    for (i = 0U; i < count; i++)
    {
        uint16_t reg = (uint16_t)(GT911_REG_POINT1 + ((uint16_t)i * GT911_POINT_SIZE));

        if (i2c_read16(s_addr, reg, buf, GT911_POINT_SIZE) != HAL_OK)
        {
            count = i;                  /* 读失败就只返回已经读到的点 */
            break;
        }

        /* buf[0]=track id, buf[1..2]=X, buf[3..4]=Y, buf[5..6]=size, buf[7]=保留 */
        touch->x[i] = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        touch->y[i] = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    }

    touch->count = count;

    /* 3) 往状态寄存器写 0 清标志，GT911 才会产生下一次上报 */
    (void)i2c_write16(s_addr, GT911_REG_STATUS, &clear, 1U);

    return 1U;                          /* 读到新数据包（count 可能为 0 = 抬起） */
}
