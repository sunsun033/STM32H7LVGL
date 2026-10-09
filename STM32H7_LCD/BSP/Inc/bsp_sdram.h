/**
  ******************************************************************************
  * @file    bsp_sdram.h
  * @brief   SDRAM (W9825G6KH-6) 驱动 + 全片自检
  *
  *  硬件对应（原理图 SCH_核心板_2026-09-30）：
  *    - 器件 : W9825G6KH-6，4 Banks x 4M x 16bit = 256Mbit = 32MB
  *    - 接口 : FMC SDRAM Bank1（片选 FMC_SDNE0 / PC2）
  *    - 基址 : 0xC0000000
  *    - 时钟 : SDCLK = HCLK/2 = 120MHz（HCLK = 240MHz）
  *    - 时序 : TMRD=2 TXSR=9 TRAS=6 TRC=8 TWR=4 TRP=2 TRCD=2
  *
  *  注意：本文件不属于 CubeMX 自动生成范围，可自由修改并在 Keil 工程中
  *        添加到独立分组（例如 "BSP"），CubeMX 重新生成代码不会影响它。
  ******************************************************************************
  */
#ifndef __BSP_SDRAM_H
#define __BSP_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* --------------------------- 硬件参数 --------------------------- */
#define BSP_SDRAM_BASE          0xC0000000UL                    /* FMC Bank1 基址 */
#define BSP_SDRAM_SIZE          (32UL * 1024UL * 1024UL)        /* 32MB           */
#define BSP_SDRAM_WORDS         (BSP_SDRAM_SIZE / 4UL)          /* 8M 个 32bit 字 */

/* SDRAM 刷新计数：
   64ms / 8192 行 = 7.8125us；7.8125us / (1/120MHz) = 937.5；937.5 - 20 = 917 */
#define BSP_SDRAM_REFRESH_CNT   917U

/* 快速自检覆盖前 1MB（几秒内出结果），全片自检覆盖整个 32MB */
#define BSP_SDRAM_QUICK_WORDS   (1UL * 1024UL * 1024UL / 4UL)

/* --------------------------- 结果定义 --------------------------- */
typedef enum
{
    BSP_SDRAM_OK = 0,
    BSP_SDRAM_FAIL_DATA_BUS,    /* 数据总线：某根 DQ 开路/短路 */
    BSP_SDRAM_FAIL_ADDR_BUS,    /* 地址总线：地址线粘连/开路   */
    BSP_SDRAM_FAIL_PATTERN,     /* 0x55AA 交替模式             */
    BSP_SDRAM_FAIL_WALKING1,    /* walking-1 全片              */
    BSP_SDRAM_FAIL_RANDOM       /* 伪随机模式                  */
} bsp_sdram_result_t;

typedef struct
{
    bsp_sdram_result_t phase;       /* 失败发生在哪个阶段      */
    uint32_t error_count;           /* 累计错误个数            */
    uint32_t first_addr;            /* 首个错误的绝对地址      */
    uint32_t expect;                /* 期望值                  */
    uint32_t actual;                /* 实际读回值              */
    uint32_t words_tested;          /* 本次测试覆盖的字数      */
} bsp_sdram_report_t;

/* 最近一次测试结果（裸机无串口时，可在 Keil 调试器里直接 watch） */
extern volatile bsp_sdram_report_t g_sdram_report;

/* 调试日志钩子：由应用层注入（可为 NULL）。
   用途：把 SDRAM 自检的进度打到串口，用来判断到底是"卡死"还是"在慢慢跑"。 */
extern void (*g_sdram_log)(const char *msg);

/* ----------------------------- API ------------------------------ */
/* 设置 SDRAM 刷新率（必须在 MX_FMC_Init() 之后调用一次，否则数据会丢） */
void bsp_sdram_init(void);

/* 清空测试报告 */
void bsp_sdram_clear_report(void);

/* 快速自检：数据总线 -> 地址总线 -> 0x55AA -> walking-1 -> 随机（覆盖前 1MB） */
bsp_sdram_result_t bsp_sdram_quick_test(void);

/* 全片自检：同上，但覆盖整个 32MB（耗时约 20~30 秒 @120MHz） */
bsp_sdram_result_t bsp_sdram_full_test(void);

/* 基础读写（offset 以 32bit 字为单位） */
void bsp_sdram_fill(uint32_t offset, uint32_t pattern, uint32_t words);
void bsp_sdram_write(uint32_t offset, const uint32_t *src, uint32_t words);
void bsp_sdram_read(uint32_t offset, uint32_t *dst, uint32_t words);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_SDRAM_H */
