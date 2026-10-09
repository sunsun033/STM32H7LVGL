/**
  ******************************************************************************
  * @file    sdram.h
  * @brief   外接 SDRAM 驱动（W9825G6KH-6，32MB / 16bit，FMC Bank1）
  *
  *  硬件对应：
  *    - 器件 : W9825G6KH-6，4 Banks x 4M x 16bit = 256Mbit = 32MB
  *    - 接口 : FMC SDRAM Bank1（片选 FMC_SDNE0），基址 0xC0000000
  *    - 时钟 : SDCLK = HCLK/2 = 120MHz（HCLK = 240MHz）
  *    - 时序 : 由 CubeMX 写进 Core/Src/fmc.c，本驱动不重复配置
  *
  *  ⚠️ 本文件不属于 CubeMX 生成范围，可自由修改，重新生成代码不会覆盖。
  ******************************************************************************
  */
#ifndef __SDRAM_H
#define __SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "fmc.h"

/* ============================ 硬件参数 ============================ */
#define SDRAM_BASE      0xC0000000UL                    /* FMC Bank1 基址   */
#define SDRAM_SIZE      (32UL * 1024UL * 1024UL)        /* 32MB             */
#define SDRAM_WORDS     (SDRAM_SIZE / 4UL)              /* 8M 个 32bit 字   */

/* 自动刷新计数：64ms / 8192 行 = 7.8125us；
   7.8125us / (1/120MHz) = 937.5；937.5 - 20 = 917。
   ⚠️ HAL_SDRAM_Init() 不会写这个寄存器，必须在 sdram_init() 里补上，
      否则 SDRAM 里的数据会在几十微秒内丢失。 */
#define SDRAM_REFRESH_COUNT     917U

/* 模式寄存器值：突发长度 1、顺序方式、CAS latency = 3、标准模式、单次写
   MR = 0b0_00_011_0_000 = 0x230
   ⚠️ 这里的 CAS 必须和 Core/Src/fmc.c 里的 FMC_SDRAM_CAS_LATENCY_3 一致。 */
#define SDRAM_MODE_REGISTER     0x230U

/* 快速自检只覆盖前 1MB（1 秒内出结果）；全片自检覆盖整个 32MB */
#define SDRAM_QUICK_WORDS       (1UL * 1024UL * 1024UL / 4UL)

/* ============================ 结果定义 ============================ */
typedef enum
{
    SDRAM_OK = 0,
    SDRAM_FAIL_DATA_BUS,        /* 数据总线：某根 DQ 开路 / 短路 */
    SDRAM_FAIL_ADDR_BUS,        /* 地址总线：地址线粘连 / 开路   */
    SDRAM_FAIL_PATTERN,         /* 0x55AA 交替模式               */
    SDRAM_FAIL_WALKING1,        /* walking-1 全片                */
    SDRAM_FAIL_RANDOM           /* 伪随机模式                    */
} sdram_result_t;

/* 最近一次自检的结果。裸机无串口时，可直接在 Keil 调试器里 watch 这个变量 */
typedef struct
{
    uint32_t errors;            /* 累计错误个数       */
    uint32_t first_addr;        /* 首个出错的绝对地址 */
    uint32_t expect;            /* 期望值             */
    uint32_t actual;            /* 实际读回值         */
} sdram_report_t;

extern volatile sdram_report_t g_sdram_report;

/* 进度日志钩子：由应用层注入（可为 NULL，也可以不注入）。
   用途：区分"卡死"和"在慢慢跑" —— 每进入一个测试阶段会调用一次。 */
extern void (*g_sdram_log)(const char *msg);

/* ============================== API ============================== */

/**
  * @brief  SDRAM 上电初始化：执行 JEDEC 上电序列 + 设置自动刷新率
  * @note   必须在 MX_FMC_Init() 之后调用一次。
  *         CubeMX 生成的 HAL_SDRAM_Init() 只写了 SDCR / SDTR，并不会让 SDRAM
  *         颗粒真正启动（CKE 一直是低电平），少了下面的序列，读写全部无效。
  */
void sdram_init(void);

/* 快速自检：数据总线 -> 地址总线 -> 0x55AA -> 伪随机（覆盖前 1MB） */
sdram_result_t sdram_test_quick(void);

/* 全片自检：同上，另加 32 轮 walking-1，覆盖整个 32MB（约 20~30 秒 @120MHz） */
sdram_result_t sdram_test_full(void);

/* 基础读写（offset 以 32bit 字为单位）。SDRAM 由 MPU 配成非缓存，
   所以这里不需要任何 cache 维护；若日后改成可缓存，见 sdram_sync_cache()。 */
void sdram_write(uint32_t offset, const uint32_t *src, uint32_t words);
void sdram_read(uint32_t offset, uint32_t *dst, uint32_t words);

#ifdef __cplusplus
}
#endif
#endif /* __SDRAM_H */
