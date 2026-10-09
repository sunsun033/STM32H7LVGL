/**
  ******************************************************************************
  * @file    bsp_sdram.c
  * @brief   SDRAM (W9825G6KH-6, 32MB/16bit, FMC Bank1) 驱动与全片自检
  ******************************************************************************
  */
#include "bsp_sdram.h"
#include "fmc.h"        /* hsdram1 */

/* --------------------------- 全局结果 --------------------------- */
volatile bsp_sdram_report_t g_sdram_report;

/* 调试日志钩子（由应用层注入，可为 NULL�?*/
void (*g_sdram_log)(const char *msg) = 0;

#define SDRAM_LOG(s)   do { if (g_sdram_log != 0) { g_sdram_log(s); } } while (0)

/* --------------------------- 内部工具 --------------------------- */

/* 取第 idx �?32bit 字的指针（volatile 防止编译器优化掉测试读写�?*/
static inline volatile uint32_t *sdram_wptr(uint32_t idx)
{
    return (volatile uint32_t *)(BSP_SDRAM_BASE + (idx * 4UL));
}

/* 数据同步屏障�?
   注意：不能用 GCC 风格�?__asm volatile ("dsb" ::: "memory")�?
         ARM Compiler 5 (armcc) 不支持那种语法（会报 #18: expected a ")"）�?
         CMSIS �?__DSB() 内部已按编译器分别实现，这里直接用它�?*/
static inline void sdram_dsb(void)
{
    __DSB();
}

/* ---------------------------------------------------------------------------
   DCache 整体 Clean + Invalidate —�?直接操作 SCB 寄存器，带完成等待与超时�?
   ---------------------------------------------------------------------------
   为什么不�?CMSIS �?SCB_CleanInvalidateDCache()�?
     本工�?main.c 里只调用�?MPU_Config()�?*从未调用 SCB_EnableDCache()**�?
     �?D-Cache 实际处于关闭状态。在"缓存未使�?的状态下触发维护操作�?
     等待完成标志可能永远等不�?—�?表现就是�?
         [addr] cache sync ...
     之后再也没有输出（卡死）�?

   这里做两件事�?
     1) 只有确认 D-Cache 真的使能了才发起维护操作�?
     2) 每一步都带超时，绝不死等�?
   --------------------------------------------------------------------------- */
#define SDRAM_CM_GUARD   100000UL

/* SCB 缓存维护寄存器的偏移(ARMv7-M)�?
   注意：CMSIS �?SCB 结构体里没有 DCISW/DCCSW 成员，所以按偏移直接访问�?*/
#define SDC_SCB_BASE     0xE000ED00UL
#define SDC_CCSIDR_OFF   0x80UL
#define SDC_CSSELR_OFF   0x84UL
#define SDC_DCISW_OFF    0x60UL
#define SDC_DCCSW_OFF    0x68UL
#define SDC_CM_REG(off)  (*(volatile uint32_t *)(SDC_SCB_BASE + (off)))

static void sdram_cache_sync(void)
{
    uint32_t ccsidr, ways, sets;
    uint32_t w, s;
    volatile uint32_t guard;

    /* D-Cache 没使能时什么都不用做（此时 CPU 读写直达 SDRAM 颗粒�?*/
    if ((SCB->CCR & (1UL << 16)) == 0UL)
    {
        sdram_dsb();
        return;
    }

    SDC_CM_REG(SDC_CSSELR_OFF) = 0UL;            /* 选择数据缓存 */
    sdram_dsb();

    ccsidr = SDC_CM_REG(SDC_CCSIDR_OFF);
    ways   = ((ccsidr >> 3) & 0x3FFUL) + 1UL;      /* 路数 */
    sets   = ((ccsidr >> 13) & 0x7FFFUL) + 1UL;    /* 组数 */

    for (s = 0UL; s < sets; s++)
    {
        for (w = 0UL; w < ways; w++)
        {
            SDC_CM_REG(SDC_DCISW_OFF) = ((s << 30) | (w << 0));
            sdram_dsb();
            guard = SDRAM_CM_GUARD;
            while ((SDC_CM_REG(SDC_DCCSW_OFF) != 0UL) && (guard != 0UL)) { guard--; }
        }
    }
    sdram_dsb();
}

/* 记录错误（只保留第一个错误的现场，后续只累加计数�?*/
static void sdram_report_error(bsp_sdram_result_t phase, uint32_t word_idx,
                               uint32_t expect, uint32_t actual)
{
    if (g_sdram_report.error_count == 0UL)
    {
        g_sdram_report.phase      = phase;
        g_sdram_report.first_addr = BSP_SDRAM_BASE + (word_idx * 4UL);
        g_sdram_report.expect     = expect;
        g_sdram_report.actual     = actual;
    }
    g_sdram_report.error_count++;
}

/* ========================== 各测试阶�?========================== */

/* 1) 数据总线：在同一个地址上做 walking-1 / walking-0�?
      可以定位是哪一�?DQ 线开路或与相�?DQ 短路�?*/
static void test_data_bus(void)
{
    volatile uint32_t *p = sdram_wptr(0UL);
    uint32_t pat, rd;

    *p = 0x00000000UL;

    for (pat = 1UL; pat != 0UL; pat <<= 1UL)          /* walking-1 */
    {
        *p = pat;
        rd = *p;
        if (rd != pat)
        {
            /* 实际值里多出来的 1，就是和�?DQ 短路的线 */
            sdram_report_error(BSP_SDRAM_FAIL_DATA_BUS, 0UL, pat, rd);
        }
    }

    for (pat = 1UL; pat != 0UL; pat <<= 1UL)          /* walking-0 */
    {
        *p = ~pat;
        rd = *p;
        if (rd != ~pat)
        {
            sdram_report_error(BSP_SDRAM_FAIL_DATA_BUS, 0UL, ~pat, rd);
        }
    }
}

/* 2) 地址总线：每个字写它自己的索引值，再逐个读回比对�?
      任何地址线粘�?开路都会导致两个地址互相覆盖�?

   ⚠️ �?/ cache 同步 / �?三个阶段都打进度点，用途：
       - 区分"彻底卡死"�?在很慢地前进"
       - 精确定位卡在哪个阶段、哪个地址
      �?8192 �?32bit 字（= 32KB 地址跨度）打一个点�?
      1MB 范围内共 32 个点。第 N 个点对应的偏�?= N * 32KB�?*/
static void test_addr_bus(uint32_t words)
{
    uint32_t i, rd;

    SDRAM_LOG("  [addr] write phase: ");
    for (i = 0UL; i < words; i++)
    {
        *sdram_wptr(i) = i;
        if ((i & 0x1FFFU) == 0UL) { SDRAM_LOG("."); }
    }
    SDRAM_LOG(" done\r\n");

    SDRAM_LOG("  [addr] cache sync ...");
    sdram_cache_sync();
    SDRAM_LOG(" ok\r\n");

    SDRAM_LOG("  [addr] read phase: ");
    for (i = 0UL; i < words; i++)
    {
        rd = *sdram_wptr(i);
        if (rd != i)
        {
            sdram_report_error(BSP_SDRAM_FAIL_ADDR_BUS, i, i, rd);
        }
        if ((i & 0x1FFFU) == 0UL) { SDRAM_LOG("."); }
    }
    SDRAM_LOG(" scan done\r\n");
}

/* 3) 0x55AA 交替：相邻字写反相模式，覆盖最大的位翻转压�?*/
static void test_55aa(uint32_t words)
{
    uint32_t i, rd;

    /* 相位 A�?x55555555 / 0xAAAAAAAA */
    for (i = 0UL; i < words; i += 2UL)
    {
        *sdram_wptr(i)       = 0x55555555UL;
        *sdram_wptr(i + 1UL) = 0xAAAAAAAAUL;
    }
    sdram_cache_sync();
    for (i = 0UL; i < words; i += 2UL)
    {
        rd = *sdram_wptr(i);
        if (rd != 0x55555555UL) { sdram_report_error(BSP_SDRAM_FAIL_PATTERN, i, 0x55555555UL, rd); }
        rd = *sdram_wptr(i + 1UL);
        if (rd != 0xAAAAAAAAUL) { sdram_report_error(BSP_SDRAM_FAIL_PATTERN, i + 1UL, 0xAAAAAAAAUL, rd); }
    }

    /* 相位 B：反�?*/
    for (i = 0UL; i < words; i += 2UL)
    {
        *sdram_wptr(i)       = 0xAAAAAAAAUL;
        *sdram_wptr(i + 1UL) = 0x55555555UL;
    }
    sdram_cache_sync();
    for (i = 0UL; i < words; i += 2UL)
    {
        rd = *sdram_wptr(i);
        if (rd != 0xAAAAAAAAUL) { sdram_report_error(BSP_SDRAM_FAIL_PATTERN, i, 0xAAAAAAAAUL, rd); }
        rd = *sdram_wptr(i + 1UL);
        if (rd != 0x55555555UL) { sdram_report_error(BSP_SDRAM_FAIL_PATTERN, i + 1UL, 0x55555555UL, rd); }
    }
}

/* 4) walking-1 全片�?2 �?bit 位各扫一遍，覆盖所有地址 */
static void test_walking1(uint32_t words)
{
    uint32_t bit, i, pat, rd;

    for (bit = 0UL; bit < 32UL; bit++)
    {
        pat = 1UL << bit;

        SDRAM_LOG(".");                 /* 每轮打一个点：能看出是在前进还是卡死 */

        for (i = 0UL; i < words; i++)
        {
            *sdram_wptr(i) = pat;
        }
        sdram_cache_sync();

        for (i = 0UL; i < words; i++)
        {
            rd = *sdram_wptr(i);
            if (rd != pat)
            {
                sdram_report_error(BSP_SDRAM_FAIL_WALKING1, i, pat, rd);
            }
        }
    }
}

/* 5) 伪随机：�?LCG 生成序列，读写两遍必须完全一�?*/
static void test_random(uint32_t words)
{
    uint32_t i, rd, seed;

    seed = 0x12345678UL;
    for (i = 0UL; i < words; i++)
    {
        seed = (seed * 1664525UL) + 1013904223UL;
        *sdram_wptr(i) = seed;
    }
    sdram_cache_sync();

    seed = 0x12345678UL;
    for (i = 0UL; i < words; i++)
    {
        seed = (seed * 1664525UL) + 1013904223UL;
        rd = *sdram_wptr(i);
        if (rd != seed)
        {
            sdram_report_error(BSP_SDRAM_FAIL_RANDOM, i, seed, rd);
        }
    }
}

/* ========================== 对外接口 ========================== */

void bsp_sdram_clear_report(void)
{
    g_sdram_report.phase        = BSP_SDRAM_OK;
    g_sdram_report.error_count  = 0UL;
    g_sdram_report.first_addr   = 0UL;
    g_sdram_report.expect       = 0UL;
    g_sdram_report.actual       = 0UL;
    g_sdram_report.words_tested = 0UL;
}

void bsp_sdram_init(void)
{
    FMC_SDRAM_CommandTypeDef cmd;

    /* ============ 1) SDRAM 上电初始化序�?============
       ⚠️ CubeMX 生成�?MX_FMC_Init() 里只�?HAL_SDRAM_Init()，它仅仅写了
          SDCR / SDTR 两个配置寄存器，**并不会让 SDRAM 粒子真正启动**�?
          缺了下面这四步，SDRAM �?CKE 一直是低电平，粒子处于掉电状态，
          读写全部无效（表现为自检莫名其妙跑不�?/ 结果全错）�?

       顺序（W9825G6KH 数据手册要求）：
          CLK_ENABLE -> 延时 >=100us -> PALL -> AUTOREFRESH x8 -> LOAD_MODE
    */
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1U;
    cmd.ModeRegisterDefinition = 0U;

    /* 1.1 时钟使能命令：把 SDRAM �?CKE 拉高 */
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 1.2 数据手册要求 >= 100us，这里给�?1ms */
    HAL_Delay(1U);

    /* 1.3 预充电所�?bank */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 1.4 自动刷新 8 �?*/
    cmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8U;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* 1.5 写模式寄存器�?
           W9825G6KH-6 -> 突发长度 = 1、顺序方式、CAS latency = 3、标准模式、单次写
           MR = 0b0_00_011_0_000 = 0x230
           （CAS 必须�?MX_FMC_Init() 里的 FMC_SDRAM_CAS_LATENCY_3 一致） */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber      = 1U;
    cmd.ModeRegisterDefinition = 0x230U;
    (void)HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU);

    /* ============ 2) 设置自动刷新计数 ============
       CubeMX 同样不会生成这句，�?SDRTR.COUNT 复位值为 0（不刷新），
       漏掉的话 SDRAM 里的数据会在几十毫秒内丢失�?*/
    (void)HAL_SDRAM_ProgramRefreshRate(&hsdram1, BSP_SDRAM_REFRESH_CNT);
}

/* 按顺序跑完全部阶段，遇到失败立即返回，方便定�?*/
static bsp_sdram_result_t bsp_sdram_run(uint32_t words)
{
    bsp_sdram_clear_report();
    g_sdram_report.words_tested = words;

    SDRAM_LOG("[sdram] 1/5 data bus (walking 1 / walking 0)\r\n");
    test_data_bus();
    if (g_sdram_report.error_count != 0UL) { return BSP_SDRAM_FAIL_DATA_BUS; }

    SDRAM_LOG("[sdram] 2/5 address bus\r\n");
    test_addr_bus(words);
    if (g_sdram_report.error_count != 0UL) { return BSP_SDRAM_FAIL_ADDR_BUS; }

    SDRAM_LOG("[sdram] 3/5 0x55AA pattern\r\n");
    test_55aa(words);
    if (g_sdram_report.error_count != 0UL) { return BSP_SDRAM_FAIL_PATTERN; }

    SDRAM_LOG("[sdram] 4/5 walking-1, 32 rounds: ");
    test_walking1(words);
    SDRAM_LOG(" done\r\n");
    if (g_sdram_report.error_count != 0UL) { return BSP_SDRAM_FAIL_WALKING1; }

    SDRAM_LOG("[sdram] 5/5 pseudo random\r\n");
    test_random(words);
    if (g_sdram_report.error_count != 0UL) { return BSP_SDRAM_FAIL_RANDOM; }

    SDRAM_LOG("[sdram] ALL PASS\r\n");
    return BSP_SDRAM_OK;
}

bsp_sdram_result_t bsp_sdram_quick_test(void)
{
    return bsp_sdram_run(BSP_SDRAM_QUICK_WORDS);
}

bsp_sdram_result_t bsp_sdram_full_test(void)
{
    return bsp_sdram_run(BSP_SDRAM_WORDS);
}

/* --------------------------- 基础读写 --------------------------- */

void bsp_sdram_fill(uint32_t offset, uint32_t pattern, uint32_t words)
{
    uint32_t i;

    for (i = 0UL; i < words; i++)
    {
        *sdram_wptr(offset + i) = pattern;
    }
    /* 需要让外部主设备（LTDC/DMA 等）看到数据时，必须回写 Cache */
    SCB_CleanDCache_by_Addr((uint32_t *)(BSP_SDRAM_BASE + offset * 4UL),
                            (int32_t)(words * 4UL));
}

void bsp_sdram_write(uint32_t offset, const uint32_t *src, uint32_t words)
{
    uint32_t i;

    for (i = 0UL; i < words; i++)
    {
        *sdram_wptr(offset + i) = src[i];
    }
    SCB_CleanDCache_by_Addr((uint32_t *)(BSP_SDRAM_BASE + offset * 4UL),
                            (int32_t)(words * 4UL));
}

void bsp_sdram_read(uint32_t offset, uint32_t *dst, uint32_t words)
{
    uint32_t i;

    /* 读之前失效对�?Cache 行，保证拿到的是 SDRAM 颗粒里的真实数据 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)(BSP_SDRAM_BASE + offset * 4UL),
                                 (int32_t)(words * 4UL));
    for (i = 0UL; i < words; i++)
    {
        dst[i] = *sdram_wptr(offset + i);
    }
}

