/**
  ******************************************************************************
  * @file    sdram.c
  * @brief   外接 SDRAM 驱动实现（W9825G6KH-6，32MB / 16bit，FMC Bank1）
  ******************************************************************************
  */
#include "sdram.h"

/* ============================ 全局结果 ============================ */
volatile sdram_report_t g_sdram_report;

/* 进度日志钩子：由应用层注入，可为 NULL */
void (*g_sdram_log)(const char *msg) = 0;

#define SDRAM_LOG(s)    do { if (g_sdram_log != 0) { g_sdram_log(s); } } while (0)

/* ============================ 内部工具 ============================ */

/* 取第 idx 个 32bit 字的指针。
   volatile 是必须的：自检里写进去再读回来，不加 volatile 会被编译器优化掉。 */
static volatile uint32_t *word_ptr(uint32_t idx)
{
    return (volatile uint32_t *)(SDRAM_BASE + (idx * 4UL));
}

/* Cache 维护。
   本工程 MPU 把整个 SDRAM 配成 Non-cacheable + Bufferable，
   CPU / DMA2D / LTDC 看到的永远是同一份数据，所以正常路径什么都不用做。

   只有把 SDRAM 改回可缓存时才需要这里。用 CMSIS 接口并**先判断 D-Cache 是否真的
   使能** —— 未使能时等待维护完成标志会永远等不到（本项目踩过这个坑，现象是卡死在
   地址总线自检里）。 */
static void sdram_sync_cache(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanInvalidateDCache();
    }
}

/* 记录错误：只保留第一个错误的现场，后面只累加计数 */
static void report_error(uint32_t word_idx, uint32_t expect, uint32_t actual)
{
    if (g_sdram_report.errors == 0UL)
    {
        g_sdram_report.first_addr = SDRAM_BASE + (word_idx * 4UL);
        g_sdram_report.expect     = expect;
        g_sdram_report.actual     = actual;
    }
    g_sdram_report.errors++;
}

/* ============================ 各测试阶段 ============================ */

/* 1) 数据总线：在同一个地址上做 walking-1 / walking-0。
      读回值里多出来的 1，就是和被测 DQ 短路的那根线；少掉的 1 说明它开路。 */
static void test_data_bus(void)
{
    volatile uint32_t *p = word_ptr(0UL);
    uint32_t pat;
    uint32_t rd;

    for (pat = 1UL; pat != 0UL; pat <<= 1UL)
    {
        *p = pat;
        rd = *p;
        if (rd != pat) { report_error(0UL, pat, rd); }
    }

    for (pat = 1UL; pat != 0UL; pat <<= 1UL)
    {
        *p = ~pat;
        rd = *p;
        if (rd != ~pat) { report_error(0UL, ~pat, rd); }
    }
}

/* 2) 地址总线：每个字写入自己的索引，再逐个读回比对。
      任何地址线粘连 / 开路都会让两个地址互相覆盖。 */
static void test_addr_bus(uint32_t words)
{
    uint32_t i;
    uint32_t rd;

    for (i = 0UL; i < words; i++) { *word_ptr(i) = i; }
    sdram_sync_cache();

    for (i = 0UL; i < words; i++)
    {
        rd = *word_ptr(i);
        if (rd != i) { report_error(i, i, rd); }
    }
}

/* 3) 0x55AA 交替：相邻两个字写相反的模式，对位翻转压力最大。
      先写 (0x5555.., 0xAAAA..)，再整体反相写一遍。 */
static void test_pattern(uint32_t words)
{
    static const uint32_t pat[2] = { 0x55555555UL, 0xAAAAAAAAUL };
    uint32_t i;
    uint32_t rd;
    uint32_t phase;

    for (phase = 0UL; phase < 2UL; phase++)
    {
        for (i = 0UL; (i + 1UL) < words; i += 2UL)
        {
            *word_ptr(i)        = pat[phase];
            *word_ptr(i + 1UL)  = pat[phase ^ 1UL];
        }
        sdram_sync_cache();

        for (i = 0UL; (i + 1UL) < words; i += 2UL)
        {
            rd = *word_ptr(i);
            if (rd != pat[phase]) { report_error(i, pat[phase], rd); }

            rd = *word_ptr(i + 1UL);
            if (rd != pat[phase ^ 1UL]) { report_error(i + 1UL, pat[phase ^ 1UL], rd); }
        }
    }
}

/* 4) 伪随机：用线性同余(LCG)生成序列，读回必须和写入完全一致 */
static void test_random(uint32_t words)
{
    uint32_t i;
    uint32_t rd;
    uint32_t seed;

    seed = 0x12345678UL;
    for (i = 0UL; i < words; i++)
    {
        seed = (seed * 1664525UL) + 1013904223UL;
        *word_ptr(i) = seed;
    }
    sdram_sync_cache();

    seed = 0x12345678UL;
    for (i = 0UL; i < words; i++)
    {
        seed = (seed * 1664525UL) + 1013904223UL;
        rd = *word_ptr(i);
        if (rd != seed) { report_error(i, seed, rd); }
    }
}

/* 5) walking-1 全片：32 个 bit 各扫一遍，覆盖全部地址（最耗时，只有全片自检才做） */
static void test_walking1(uint32_t words)
{
    uint32_t bit;
    uint32_t i;
    uint32_t pat;
    uint32_t rd;

    for (bit = 0UL; bit < 32UL; bit++)
    {
        pat = 1UL << bit;

        SDRAM_LOG(".");             /* 每轮打一个点：能看出是在前进还是卡死 */

        for (i = 0UL; i < words; i++) { *word_ptr(i) = pat; }
        sdram_sync_cache();

        for (i = 0UL; i < words; i++)
        {
            rd = *word_ptr(i);
            if (rd != pat) { report_error(i, pat, rd); }
        }
    }
}

/* ============================ 对外接口 ============================ */

void sdram_init(void)
{
    FMC_SDRAM_CommandTypeDef cmd;

    /* JEDEC 上电序列（W9825G6KH 数据手册要求）：
         CLK_ENABLE -> 延时 >=100us -> PALL -> AUTOREFRESH x8 -> LOAD_MODE
       顺序不能变。 */
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1U;
    cmd.ModeRegisterDefinition = 0U;

    /* 1) 时钟使能：把 SDRAM 的 CKE 拉高 */
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 2) 手册要求 >=100us，这里给足 1ms */
    HAL_Delay(1U);

    /* 3) 预充电所有 bank */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 4) 自动刷新 8 次 */
    cmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8U;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 5) 写模式寄存器 */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber      = 1U;
    cmd.ModeRegisterDefinition = SDRAM_MODE_REGISTER;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 6) 设置自动刷新计数（SDRTR.COUNT 复位值为 0 = 不刷新，必须补上） */
    (void)HAL_SDRAM_ProgramRefreshRate(&hsdram1, SDRAM_REFRESH_COUNT);
}

/* 按顺序跑完各个阶段，遇到失败立即返回，方便定位 */
static sdram_result_t run_test(uint32_t words, uint8_t with_walking1)
{
    g_sdram_report.errors     = 0UL;
    g_sdram_report.first_addr = 0UL;
    g_sdram_report.expect     = 0UL;
    g_sdram_report.actual     = 0UL;

    SDRAM_LOG("[sdram] 1/5 data bus (walking 1 / 0)\r\n");
    test_data_bus();
    if (g_sdram_report.errors != 0UL) { return SDRAM_FAIL_DATA_BUS; }

    SDRAM_LOG("[sdram] 2/5 address bus\r\n");
    test_addr_bus(words);
    if (g_sdram_report.errors != 0UL) { return SDRAM_FAIL_ADDR_BUS; }

    SDRAM_LOG("[sdram] 3/5 0x55AA pattern\r\n");
    test_pattern(words);
    if (g_sdram_report.errors != 0UL) { return SDRAM_FAIL_PATTERN; }

    SDRAM_LOG("[sdram] 4/5 pseudo random\r\n");
    test_random(words);
    if (g_sdram_report.errors != 0UL) { return SDRAM_FAIL_RANDOM; }

    if (with_walking1 != 0U)
    {
        SDRAM_LOG("[sdram] 5/5 walking-1 x32\r\n");
        test_walking1(words);
        if (g_sdram_report.errors != 0UL) { return SDRAM_FAIL_WALKING1; }
    }

    SDRAM_LOG("[sdram] ALL PASS\r\n");
    return SDRAM_OK;
}

sdram_result_t sdram_test_quick(void)
{
    return run_test(SDRAM_QUICK_WORDS, 0U);
}

sdram_result_t sdram_test_full(void)
{
    return run_test(SDRAM_WORDS, 1U);
}

/* ============================ 基础读写 ============================ */

void sdram_write(uint32_t offset, const uint32_t *src, uint32_t words)
{
    uint32_t i;

    for (i = 0UL; i < words; i++)
    {
        *word_ptr(offset + i) = src[i];
    }
    /* 让外部主设备（LTDC / DMA 等）看到数据：可缓存时才需要 */
    sdram_sync_cache();
}

void sdram_read(uint32_t offset, uint32_t *dst, uint32_t words)
{
    uint32_t i;

    /* 读之前失效对应 Cache 行，保证拿到的是 SDRAM 颗粒里的真实数据 */
    sdram_sync_cache();

    for (i = 0UL; i < words; i++)
    {
        dst[i] = *word_ptr(offset + i);
    }
}
