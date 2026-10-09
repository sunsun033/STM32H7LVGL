/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma2d.h"
#include "i2c.h"
#include "ltdc.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sdram.h"      /* SDRAM 驱动 + 自检      */
#include "lcd.h"        /* LTDC + DMA2D 屏幕驱动、背光调光 */
#include "gt911.h"      /* GT911 电容触摸         */
#include "lv_port.h"    /* LVGL 9.3 移植层         */
#include "ui.h"         /* 界面：背景图 + 灯图标 + 亮度滑条 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* SDRAM 上电自检：1 = 每次上电都做一次快速自检（1 秒内出结果）
                  0 = 跳过（板子已验证过，启动更快）
   ⚠️ 不管开关怎么设，sdram_init()（JEDEC 上电序列 + 刷新率）都必须保留。 */
#define SDRAM_SELFTEST_EN   1

/* 缓存开关（在 Cache_Enable() 里生效）：
   I-Cache 打开 —— 代码在 Flash 里执行，收益最大，且没有数据一致性风险。
   D-Cache 关闭 —— SDRAM（显存）由 MPU 配成 Non-cacheable，CPU / DMA2D / LTDC
                   视图天然一致，不需要任何 cache 维护代码；数据又基本都在
                   DTCM / AXI SRAM，D-Cache 收益很小。 */
#define LCD_ICACHE_EN       1
#define LCD_DCACHE_EN       0

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==================== 后台辅助函数 ==================== */

/* 把字符串打到 USART1（115200-8-N-1），同时作为驱动层日志钩子的实现。
   这里用 LVGL 的 lv_strlen，避免依赖 newlib（本工程的 LVGL 标准库是自带的） */
static void log_puts(const char *msg)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)lv_strlen(msg), 100U);
}

/* 缓存开关：必须在 MPU_Config() 之后调用。
   详细理由见文件开头 LCD_ICACHE_EN / LCD_DCACHE_EN 的注释。 */
static void Cache_Enable(void)
{
#if (LCD_ICACHE_EN != 0)
    SCB_EnableICache();
#endif
#if (LCD_DCACHE_EN != 0)
    SCB_EnableDCache();
#endif
}

/* ==================== 界面：背光亮度滑条 ====================
   界面代码已经搬到 UI/ui.c（背景图 + 灯图标 + 亮度滑条，两个界面带切换动画），
   这里只调用 ui_init()。 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* MPU 已经配好，现在打开缓存（必须放在 MPU_Config() 之后） */
  Cache_Enable();
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FMC_Init();
  MX_DMA2D_Init();
  MX_I2C2_Init();
  MX_LTDC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* ---------------- 1) SDRAM：上电序列（必须）+ 可选自检 ----------------
     ⚠️ 自检会覆盖整个 SDRAM，所以必须排在 lcd_init()/LVGL 之前，
        否则会把已经画好的显存冲掉。 */
  sdram_init();

#if (SDRAM_SELFTEST_EN != 0)
  g_sdram_log = log_puts;               /* 打开进度输出，便于区分"卡死"和"在慢慢跑" */
  log_puts("[main] SDRAM quick selftest ...\r\n");
  if (sdram_test_quick() != SDRAM_OK)
  {
    log_puts("[main] SDRAM selftest FAILED\r\n");
    Error_Handler();                    /* 失败详情在 g_sdram_report 里，Keil 里 watch */
  }
  g_sdram_log = 0;
#endif

  /* ---------------- 2) 屏幕：配 DMA2D、开 LTDC 换页中断、清双缓冲 ----------------
     ⚠️ 这里故意不开背光，见 lcd.h 里说明的启动顺序。 */
  lcd_init();

  /* ---------------- 3) LVGL：tick + 显示（双缓冲直接渲染）+ 触摸 ---------------- */
  lv_port_init();

  /* 先把第一帧画好 —— 此时背光还关着，用户看不见。
     界面代码在 UI/ui.c：主界面（背景图 + 灯图标）按下后带动画切到亮度滑条界面。 */
  ui_init();
  (void)lv_timer_handler();

  /* ---------------- 4) 触摸：这一步会把面板一起复位（RST 共用） ----------------
     放在开背光之前，复位造成的黑屏就看不见了。 */
  if (gt911_init() != 0U)
  {
    log_puts("[main] GT911 not found\r\n");
  }

  /* ---------------- 5) 所有复位动作都做完了，开背光 ----------------
     背光亮度由背光 PWM（TIM3_CH3 / PB0）的占空比决定，见 Hardware/lcd.c。
     100 = 最亮。之后拖动界面上的滑条会实时改这个亮度。 */
  lcd_backlight_set(100U);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* LVGL 主任务：处理定时器、输入设备、界面刷新。
       返回"距离下一次需要处理的毫秒数"，据此让出 CPU，
       既不会空转把总线打满，也不会漏掉定时任务。 */
    uint32_t next_ms = lv_timer_handler();

    if (next_ms > 5U) { next_ms = 5U; }
    HAL_Delay(next_ms);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ============================== MPU 配置说明 ==============================
   ⚠️ 本段是**注释**，故意放在 USER CODE 区里 —— CubeMX 重新生成代码只覆盖
      它自己的区域，这段不会被删。真正的配置在下面的 MPU_Config()。

   | Region | 地址        | 大小  | 属性                       | 用途             |
   |--------|-------------|-------|----------------------------|------------------|
   |   0    | 0x00000000  | 4GB   | No access（背景区）        | 最小权限兜底     |
   |   1    | 0xC0000000  | 32MB  | Non-cacheable + Bufferable | SDRAM（含显存）  |
   |   2    | 0x24000000  | 512KB | Cacheable + Bufferable     | AXI SRAM         |

   ★ 为什么 SDRAM 必须整块非缓存：
     CPU 访问 SDRAM 会经 D-Cache，而 LTDC / DMA2D 是直接读写 SDRAM 颗粒
     （不经过 CPU Cache）。三方对同一块显存的视图必须一致，否则花屏 / 残影。
     设成 Non-cacheable + Bufferable 之后：
       - 三方视图永远一致，**不需要任何 cache 维护代码**；
       - Bufferable 仍允许写合并，写吞吐不差。

   ★ 现状（别被 LCD_DCACHE_EN = 0 迷惑）：
     本工程当前**关着 D-Cache**（见文件开头的 LCD_DCACHE_EN），所以这两个
     Region 的属性眼下"用不上"。但它必须提前配对 —— 哪天把 D-Cache 打开，
     配错就会立刻花屏，而且很难往 MPU 上想。

   ⚠️ 不要再用"同基址再叠一个 4MB 非缓存区去覆盖显存"的老办法 ——
      实测显存仍会被缓存，画面变成青色噪声带 + 浅蓝带（BSP 笔记 Bug 5）。

   ★ 如果 CubeMX 重新生成后 MPU_Config() 变得"很短"（Region1 只写几个字段），
     别急着补赋值：MPU_InitStruct 是 {0} 清零的，而
        MPU_ACCESS_NOT_CACHEABLE == 0x0、MPU_ACCESS_NOT_SHAREABLE == 0x0、
        MPU_ACCESS_NOT_BUFFERABLE == 0x0，
     "没写"就等于"非缓存 / 不共享 / 不缓冲"；Region2 也会沿用 Region1 赋过的
     那几个字段。两版写法**功能完全等价**。

   ★ 检查清单（改动 MPU 后逐项核对 Project.ioc 的 CORTEX_M7.* 键）：
       BaseAddress-...Region1_Settings = 0xC0000000
       Size-...Region1_Settings        = MPU_REGION_SIZE_32MB
       IsCacheable-...Region1_Settings = MPU_ACCESS_NOT_CACHEABLE
       IsBufferable-...Region1_Settings= MPU_ACCESS_BUFFERABLE
       BaseAddress-...Region2_Settings = 0x24000000
       IsCacheable-...Region2_Settings = MPU_ACCESS_CACHEABLE
       MPU_Control                     = MPU_PRIVILEGED_DEFAULT
     Keil 调试时也能直接 watch 实际生效值：
       SCB->CCR（bit16 = D-Cache 是否使能）、MPU->RBAR / MPU->RASR。
   ======================================================================== */

/* USER CODE END 4 */

 /* MPU Configuration */

/* ============================== MPU 配置 ==============================
   三个区域，**互不重叠**（重叠区域的优先级行为不可靠，本项目踩过坑）。

   | Region | 地址         | 大小  | 属性                       | 用途            |
   |--------|--------------|-------|----------------------------|-----------------|
   |   0    | 0x00000000   | 4GB   | No access（背景区）        | 最小权限兜底    |
   |   1    | 0xC0000000   | 32MB  | Non-cacheable + Bufferable | SDRAM（含显存） |
   |   2    | 0x24000000   | 512KB | Cacheable + Bufferable     | AXI SRAM        |

   ★ 为什么 SDRAM 必须整体非缓存：
     CPU 访问 SDRAM 会经过 D-Cache，而 LTDC / DMA2D 是直接读写 SDRAM 颗粒
     （不经过 CPU Cache）。两者对同一块显存的视图必须一致，否则花屏 / 残影。
     整块设成 Non-cacheable + Bufferable 之后：
       - 三方视图永远一致，**不需要任何 cache 维护代码**
       - Bufferable 仍允许写合并，写吞吐不差

   ⚠️ 不要再用"同基址叠一个 4MB 非缓存区去覆盖显存"的老办法 ——
      实测显存仍会被缓存，画面变成青色噪声带 + 浅蓝带（BSP 笔记 Bug 5）。

   ⚠️ 本函数是 CubeMX 生成的。若以后用 CubeMX 重新生成代码，
      一定要在 CubeMX 的 MPU 配置里同步成上面这张表，否则会被覆盖回去。
   ====================================================================== */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /* Region 0：4GB 背景区，先设成"禁止访问"再靠后面的区域开洞。
     SubRegionDisable = 0x87 -> 关掉子区 0/1/2/7，只保留子区 3~6。
     这样 0x00000000~0x1FFFFFFF（Flash）和 0x20000000~0x3FFFFFFF（DTCM/AXI SRAM）
     走默认内存映射，不受本区域影响。 */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress      = 0x00000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Region 1：整个 SDRAM 32MB（含 3MB 显存）
     Non-cacheable + Bufferable，三个主设备视图永远一致 */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress      = 0xC0000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Region 2：AXI SRAM 512KB，可缓存（要放热数据时用这块） */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress      = 0x24000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
