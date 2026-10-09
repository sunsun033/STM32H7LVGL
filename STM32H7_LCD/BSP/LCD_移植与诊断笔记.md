# LCD 移植与诊断笔记（我的板 / 7寸 800x480 18bit RGB666）

> 本文件是**手写文档**，不参与编译，CubeMX 重新生成代码不会动它。
> 放在 `BSP\` 下（BSP 目录不是 CubeMX 生成的）。
>
> **状态：已完成并验证通过** —— 八色竖条颜色与位置全部正确，触摸画点正常。
> 以后重新生成 CubeMX 代码或换屏时，重点看 **第 2、4、6 章**。

## 0. 最终配置速查（照这个配就不会错）

| 项目 | 值 | 在哪配 |
|---|---|---|
| LTDC Layer0 像素格式 | **`ARGB8888`** | **CubeMX**：LTDC → Layer Settings → Layer0 |
| `LCD_BYTES_PER_PIXEL` | **`4U`** | `BSP\Inc\bsp_lcd.h` |
| 颜色宏 `LCD_COLOR(r,g,b)` | `0xFF000000 \| r<<16 \| g<<8 \| b` | `BSP\Inc\bsp_lcd.h` |
| **DMA2D `ColorMode`** | **`DMA2D_OUTPUT_ARGB8888`** | `BSP\Src\bsp_lcd.c` |
| SDRAM `ReadBurst` | **`ENABLE`** | **CubeMX**：FMC → SDRAM1 → Read burst |
| MPU：SDRAM | **Non-cacheable + Bufferable**（不分块重叠） | `Core\Src\main.c` → `MPU_Config()` |
| 缓存 | **I-Cache 开 / D-Cache 默认关** | `Core\Src\main.c` → `LCD_ICACHE_EN` / `LCD_DCACHE_EN` |
| 启动速度 | `SDRAM_SELFTEST_EN = 0` 可跳过自检 | `Core\Src\main.c` |

**两句话记住这个工程的两个大坑**：
1. **18 位 RGB666 面板必须用 `ARGB8888`**（不是 RGB565，也不是 RGB888）。
2. **改 LTDC 像素格式时，必须同步改 DMA2D 的 `ColorMode`** —— 两者命名不一致（见第 3 章 Bug 6）。

---

## 1. 硬件事实

| 项目 | 内容 |
|---|---|
| 屏 | 7 寸，800×480，40P FPC，排线丝印 `FPC070BD40-01/B`（或 `TP070BDA0-01/R`） |
| 面板接口 | **18 位 RGB666**（每组 6 根数据线） |
| 触摸 | GT911，I2C2（PH4/PH5），INT/RST = PI3/PG13，地址 0x5D 或 0x14（实测 0x14） |
| 背光 | PB0 → R64(0Ω) → U3 MT9284-28J 的 EN（高有效） |
| SDRAM | W9825G6KH-6 32MB/16bit，FMC Bank1，基址 `0xC0000000` |
| MCU | STM32H743IIT6（LQFP176），HSE 25MHz |

### LTDC 引脚（CubeMX 生成，**注意是 R2~R7 / G2~G7 / B2~B7**）

```
PF10 -> LTDC_DE        PA4  -> LTDC_VSYNC     PC6  -> LTDC_HSYNC     PG7 -> LTDC_CLK
PH8  -> LTDC_R2        PH9  -> LTDC_R3        PH10 -> LTDC_R4
PH11 -> LTDC_R5        PH12 -> LTDC_R6        PG6  -> LTDC_R7
PH13 -> LTDC_G2        PH14 -> LTDC_G3        PH15 -> LTDC_G4
PI0  -> LTDC_G5        PI1  -> LTDC_G6        PI2  -> LTDC_G7
PG10 -> LTDC_B2        PG11 -> LTDC_B3        PI4  -> LTDC_B4
PI5  -> LTDC_B5        PI6  -> LTDC_B6        PI7  -> LTDC_B7
```

---

## 2. ★★★ 最关键结论：必须用 **ARGB8888**（不是 RGB565、也不是 RGB888）

18 位 RGB666 面板每组要 6 根数据线，而 **LTDC 在 RGB565 下每通道只输出 5/6/5 位**：

| 格式 | PFCR 值 | 红输出 | 绿输出 | 蓝输出 | 是否匹配 |
|---|---|---|---|---|---|
| RGB565 | `0x02` | R[7:3] = **5 位** | G[7:2] = 6 位 | B[7:3] = **5 位** | ❌ 红/蓝各少一位且整体错位 |
| RGB888 | `0x01` | R[7:2] = 6 位 | G[7:2] = 6 位 | B[7:2] = 6 位 | ⚠️ 位宽够，但**数据布局不匹配**（见下） |
| **ARGB8888** | **`0x00`** | R[7:2] = 6 位 | G[7:2] = 6 位 | B[7:2] = 6 位 | ✅ **位宽够 + 布局匹配** |

### 为什么 RGB888 也不行（这一步踩了很久）

我们 BSP 的颜色是 `0xAARRGGBB` 布局（`A=31:24, R=23:16, G=15:8, B=7:0`），
例如红色写成 `0x00FF0000`。

- 用 **ARGB8888(PFCR=0x00)**：LTDC 按 `A(31:24) R(23:16) G(15:8) B(7:0)` 解读
  → `R=0xFF` ✅ **红通道正确**
- 用 **RGB888(PFCR=0x01)**：LTDC 按高 24 位为 RGB 解读
  → 把 `A(31:24)=0x00` 当成了红 → **红通道恒为 0** ❌

**RGB888 下的实测故障现象**（已复现）：
```
发白色 -> 屏上显浅蓝(绿+蓝, 缺红) + 细密竖条纹
所有颜色都缺红
```

### 还有一个坑：**图层使能后 PFCR 不可写**

```
实测：层已使能时写 PFCR=0x00，立刻读回仍是 0x01 —— 写入被硬件忽略！
```
所以格式**必须在 `pLayerCfg.PixelFormat`（`HAL_LTDC_ConfigLayer()` 内部、
图层使能之前）设置**，不能在别处事后改 PFCR。

### 要改的 3 处（缺一不可）

| 位置 | 值 | 说明 |
|---|---|---|
| **CubeMX**: LTDC → Layer Settings → Layer0 → Pixel Format | **`ARGB8888`** | 写进 `.ioc`，永久生效 |
| `Core\Src\ltdc.c` → `pLayerCfg.PixelFormat` | `LTDC_PIXEL_FORMAT_ARGB8888` | 由 CubeMX 生成 |
| `BSP\Inc\bsp_lcd.h` → `LCD_BYTES_PER_PIXEL` | **`4U`** | 每像素 4 字节 |

> ⚠️ `Core\Src\ltdc.c` 是 CubeMX 生成的，**必须在 CubeMX 界面里改成 ARGB8888**，
> 否则重新生成代码会覆盖回去。

---

## 3. 已修复的软件 Bug

### Bug 1：SDRAM 自检卡死在 `[addr] cache sync ...`

- **文件**：`BSP\Src\bsp_sdram.c` → `sdram_cache_sync()`
- **原因**：调用了 CMSIS 的 `SCB_CleanInvalidateDCache()`，但**本工程从未使能 D-Cache**
  （`main.c` 只调用了 `MPU_Config()`，没有 `SCB_EnableDCache()`）→ 等待维护完成标志时死等。
- **修法**：直接操作 SCB 寄存器（偏移 `DCISW=0x60`、`DCCSW=0x68`、`CCSIDR=0x80`、`CSSELR=0x84`），
  先判断 `SCB->CCR` bit16（D-Cache 使能）——未使能直接返回；使能了才做且**每步带超时**。
  屏障用 CMSIS 的 `__DSB()`（**不能用 GCC 风格 `__asm volatile(... ::: ...)`，ARMCC 5 不支持**）。

### Bug 2：DMA2D 局部填充错位

- **文件**：`BSP\Src\bsp_lcd.c` → `bsp_lcd_fill_rect()`
- **原因**：`hdma2d.Init.OutputOffset` 被固定为 0。
  `OutputOffset` = "写完一行的 w 个像素后跳过多少像素再写下一行"。
  整屏填充（w=800）时 0 是对的；**局部填充（如彩条 w=100）时必须为 `LCD_WIDTH - w`**，
  否则每行写完 100 像素就接着写下一行，彩条被斜着连续填充、整屏错位。
- **修法**：每次填充前 `hdma2d.Init.OutputOffset = (uint32_t)(LCD_WIDTH - w);`

### Bug 3：SDRAM 突发读被禁用 → RGB888 带宽不够

- **文件**：`Core\Src\fmc.c`（CubeMX 生成）
- **原因**：`hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;`
  LTDC 是顺序扫描显存，禁用突发读时每个读命令只取一个字，带宽掉一半以上。
  RGB565 时每帧 768KB（46MB/s）勉强够；RGB888 时每帧 1.5MB（92MB/s）就出现**条纹/撕裂**。
- **修法**：改为 `FMC_SDRAM_RBURST_ENABLE`。
  **在 CubeMX 里同步**：`Connectivity → FMC → SDRAM1 → Read burst → 勾选 Enable`

### Bug 4：`HAL_DMA2D_Start()` 不会重新加载 `OOR` → 局部填充全错

- **文件**：`BSP\Src\bsp_lcd.c` → `bsp_lcd_fill_rect()`
- **原因**：`OutputOffset` **只在 `HAL_DMA2D_Init()` 里**写进 `OOR` 寄存器：
  ```c
  /* stm32h7xx_hal_dma2d.c 第 290 行，在 HAL_DMA2D_Init 内 */
  MODIFY_REG(hdma2d->Instance->OOR, DMA2D_OOR_LO, hdma2d->Init.OutputOffset);
  ```
  `HAL_DMA2D_Start()` **不碰 OOR**。所以"改 `hdma2d.Init.OutputOffset` 再 Start"是无效的，
  寄存器里永远是 Init 时写进去的 0。
- **实测症状**：
  - 整屏填充（`w = 800`，offset 应为 0）→ **正常**（正好和寄存器里的 0 一致，掩盖了问题）
  - 触摸画 8×8 方块（offset 应为 792）→ 每行 8 像素直接接着写 → **变成 64 像素的横杠**
  - 八色竖条（`w = 100`，offset 应为 700）→ 斜着连成一片
- **修法**：**直接写寄存器**
  ```c
  hdma2d.Init.OutputOffset = (uint32_t)(LCD_WIDTH - w);
  hdma2d.Instance->OOR      = (uint32_t)(LCD_WIDTH - w);   /* 必须写寄存器 */
  ```

### Bug 5：D-Cache 打开后显存数据不一致 → 花屏

- **文件**：`Core\Src\main.c` → `MPU_Config()` / `Cache_Enable()`
- **原因**：一开始把 **整个 32MB SDRAM 设成 Cacheable**，再用一个同基址的 4MB
  Non-cacheable 区域去覆盖显存。**同基址区域重叠的行为不可靠**，结果显存仍被缓存 →
  CPU/DMA2D 与 LTDC 看到的数据不一致 → **花屏**，触摸方块也被脏缓存行覆盖。
- **第二个副作用**：SDRAM 自检是**流式访问 1MB+**，而 D-Cache 只有 16KB →
  每次写新行都驱逐回写，**比不开缓存还慢** → 表现为"初始化很久"。
- **修法**：**整个 SDRAM 32MB 统一设为 Non-cacheable + Bufferable**（不做区域重叠），
  D-Cache 默认关闭、I-Cache 打开：
  ```c
  #define LCD_ICACHE_EN   1   /* 代码在 Flash，收益最大且无数据一致性风险 */
  #define LCD_DCACHE_EN   0   /* 本项目数据几乎都在 DTCM/AXI SRAM，D-Cache 收益极小 */
  ```

### Bug 6：★★ DMA2D 与 LTDC 的像素格式"命名不一致"，配错会串色

**这是最容易踩的坑之一**：两个外设对同一件事用不同的名字。

| LTDC 层格式 | **应该配的 DMA2D 输出格式** |
|---|---|
| `LTDC_PIXEL_FORMAT_ARGB8888` | **`DMA2D_OUTPUT_ARGB8888`** ✅ |
| `LTDC_PIXEL_FORMAT_RGB888` | **`DMA2D_OUTPUT_ARGB8888`**（**不是** `DMA2D_OUTPUT_RGB888`！） |
| `LTDC_PIXEL_FORMAT_RGB565` | `DMA2D_OUTPUT_RGB565` |

- **原因**：DMA2D 的 `RGB888`（`DMA2D_OPFCCR_CM_0 = 0x01`）按自己的方式打包 24 位，
  与 LTDC 的 `RGB888`（`PFCR=0x01`，存在 32 位字里）**字节序不同**。
  两者混用 → **RGB 通道错乱 + 显存地址步进错位**（表现为彩色横带、竖条错位、花屏）。
- **症状**：整屏纯色（CPU 直写）正常，但一切**经 DMA2D 绘制的图形颜色错乱**。
- **修法**：让 DMA2D 与 LTDC 用**同一套 ARGB8888**
  ```c
  /* BSP\Src\bsp_lcd.c */
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;   /* 配 LTDC 的 ARGB8888 层 */
  ```
- **教训**：**改 LTDC 像素格式时，一定要同步检查 DMA2D 的 `ColorMode`**，两者必须配对。

### Bug 7：开机时屏幕闪一下（`[CP9]` 处"重新刷新"）

- **现象**：串口打到 `[CP9] GT911 init: ...` 时，屏幕黑一下又重新显示，像被刷新了一次。
- **原因（硬件共用引脚）**：
  ```
  PG14 = LCD_TP_RST  ——  触摸复位
  PG14 = 面板 P_RESET ——  FPC 第 31 脚，面板复位
  ```
  **两者是同一根线！** `bsp_gt911_init()` 要把 PG14 拉低 20ms 复位 GT911，
  **面板同时也被复位了** → 画面黑掉 ~20ms 再恢复。
  因为此时背光已经打开，所以**肉眼能看见**。
- **修法：把开背光放到 GT911 初始化之后**
  ```c
  /* Core\Src\main.c —— 正确顺序 */
  bsp_lcd_init();            /* 配好 DMA2D + 清屏；背光仍然关着 */
  bsp_lcd_test_pattern();    /* 先把第一帧画好（此时看不见） */
  bsp_gt911_init();          /* 面板+触摸复位发生在这里，背光没开→看不见 */
  bsp_lcd_backlight(1U);     /* 全部复位动作完成后再开背光 → 无闪烁 */
  ```
  > 因此 **`bsp_lcd_init()` 故意不再自动开背光**，需要显式调用 `bsp_lcd_backlight(1U)`。
- **通用原则**：**面板/触摸复位动作必须在背光关闭期间完成**。
  凡是与面板共用复位脚的器件（这里就是 GT911），初始化都要排在开背光之前。

---

## 3b. 启动时间：SDRAM 自检开关

SDRAM 自检要流式读写 1MB 好几遍（外加 32 轮 walking-1），是启动耗时的主要来源。

```c
/* Core\Src\main.c 的 USER CODE 2 区 */
#define SDRAM_SELFTEST_EN   1   /* 1=每次上电都自检(几秒)  0=跳过(启动几乎瞬间) */
```

- **调试/新板子**：设 `1`，确认 SDRAM 可靠
- **正常使用**：设 `0` —— 板子已验证过就不必每次上电重测
  （`bsp_sdram_init()` 仍然会执行，那是 SDRAM 粒子上电序列，**必须保留**）

---
## 4. 缓存与 MPU 配置

### 为什么要特别处理

CPU 访问 SDRAM 会经过 **D-Cache**，而 **LTDC / DMA2D 直接读写 SDRAM 颗粒（不经过 CPU Cache）**。
两者对同一块显存的视图必须一致，否则就会出现**花屏 / 残影 / 触摸画点变成横杠**。

### 踩过的坑：不要用"重叠区域"去覆盖

最初的做法是：Region1 = 整个 32MB 设 Cacheable，再用 Region2 = 同基址 4MB 设 Non-cacheable
去覆盖显存那一段。**实测不可靠** —— 显存仍被缓存，画面变成"青色噪声带 + 黑 + 浅蓝带"。

### 最终方案（简单、无重叠）

| Region | 地址 | 大小 | 属性 | 用途 |
|---|---|---|---|---|
| 0 | `0x00000000` | 4GB | No access | 背景区（最小权限），`SubRegionDisable=0x87` |
| 1 | `0xC0000000` | 32MB | **Non-cacheable + Bufferable** | **SDRAM 整块**（含显存） |
| 2 | `0x24000000` | 512KB | Cacheable + Bufferable | AXI SRAM |

- SDRAM 统一 **Non-cacheable + Bufferable**：
  - Non-cacheable → CPU / DMA2D / LTDC **三者视图永远一致**，无需任何 cache 维护
  - Bufferable 仍允许写合并，写吞吐不差
  - 顺带避免了 SDRAM 自检（流式访问 1MB+）把 16KB 的 D-Cache 反复冲刷变慢
- **不做任何区域重叠** —— 这是关键，重叠优先级行为不可靠

### 缓存开关

```c
/* Core\Src\main.c */
#define LCD_ICACHE_EN   1   /* 代码在 Flash 执行，I-Cache 收益最大，且无数据一致性风险 */
#define LCD_DCACHE_EN   0   /* 默认关闭，见下 */
```

`D-Cache` 默认关闭的原因：

| 内存 | 是否经过缓存 | 说明 |
|---|---|---|
| **Flash 取指** | **I-Cache ✅ 已开** | **本工程收益最大的地方** |
| DTCM `0x20000000`（栈） | TCM 本身不经缓存 | 已经最快，无需优化 |
| AXI SRAM `0x24000000` | D-Cache（若开） | 本工程数据极少（RW=48B / ZI=2240B），收益很小 |
| SDRAM | ❌ 非缓存 | 为一致性刻意如此 |

> **如果以后要把热数据放进 AXI SRAM 并实测有收益**，把 `LCD_DCACHE_EN` 改成 1 即可 ——
> SDRAM 由 MPU 保持 Non-cacheable，显示不会受影响。
>
> **如果以后要把 SDRAM 改回可缓存**（为了 CPU 大数据吞吐），必须：
> 1. 用 MPU 把**显存那一段**单独设成 Non-cacheable（且不要用同基址重叠，划出独立地址段）
> 2. 并在每次改完显存后调用 `SCB_CleanDCache_by_Addr()`

---

## 5. Diagnostic methods (for future troubleshooting)

> 本次排障用的 3 个诊断函数（整屏纯色 / 逐位点亮 / CPU 直写+寄存器 dump）
> 在问题定位后**已从 `BSP` 清理掉**，保持代码干净。需要时按下面思路重写即可。

需要排障时把下面的测试函数从 `BSP` 里找回来用（本次排障已完成，函数已清理，
需要时可从 git/备份恢复，或按本节思路重写）：

| 测试 | 做法 | 用途 |
|---|---|---|
| 整屏纯色轮换 | 红/绿/蓝/白/黑 各 2 秒 | 判断 LTDC/屏/数据线是否正常 |
| 逐位点亮 | 每次只置位 1 个 bit 并打印 bit 号 | **反推真实像素格式** |
| CPU 直写 + 寄存器 dump | 绕开 DMA2D 逐像素写显存，打印 `L0PFCR/L0CFBLR/...` | **本次排障的决定性工具** |

**经验**：遇到显示问题，**先读 LTDC 寄存器**（`L0PFCR` 尤其重要），
比反复改代码有效得多。本次就是靠 `L0PFCR = 0x01` 和"写 0x00 读回 0x01"
两个现象定位到根因的。

---

---

## 5. 文字显示与帧率显示（已实现）

### 字库

| 项目 | 值 |
|---|---|
| 规格 | **8 × 12 像素**，每字符 12 字节（每行 1 字节，bit7 = 最左像素） |
| 字符集 | **可打印 ASCII 0x20~0x7E**（空格、数字、大小写英文、常用标点），共 95 个 |
| 大小 | 95 × 12 = **1140 字节**（`RO-data`，放 Flash） |
| 来源 | 用 GDI+ 渲染 **Consolas 11px**（`SingleBitPerPixelGridFit`，dy=-1）自动生成 |
| 位置 | `BSP\Src\bsp_lcd.c` 里的 `font8x12[95][12]` |

> 超出 0x20~0x7E 的字符**按空格绘制**，不会越界。

### 文字 API（`BSP\Inc\bsp_lcd.h`）

```c
#define LCD_FONT_W   8U
#define LCD_FONT_H   12U

void     bsp_lcd_draw_char(uint16_t x, uint16_t y, char c, lcd_color_t fg, lcd_color_t bg);
void     bsp_lcd_draw_text(uint16_t x, uint16_t y, const char *s, lcd_color_t fg, lcd_color_t bg);
uint16_t bsp_lcd_text_width(const char *s);
void     bsp_lcd_draw_text_uint(uint16_t x, uint16_t y, uint32_t v, uint8_t digits,
                                lcd_color_t fg, lcd_color_t bg);   /* 定宽右对齐, 如 7 -> "007" */
```

用法示例：

```c
bsp_lcd_draw_text(4U, 4U, "Hello 123", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
bsp_lcd_draw_text_uint(100U, 4U, 60U, 3U, LCD_COLOR_GREEN, LCD_COLOR_BLACK);  /* 显示 060 */
```

### 帧率显示（左上角）

```c
bsp_lcd_frame_init();                   /* 开 LTDC 行中断，每帧 1 次 */
uint32_t n = bsp_lcd_frame_count();     /* 累计帧数（单调递增） */
```
`main.c` 主循环里用 1 秒窗口算 FPS，画在左上角 `(4,4)`：

```c
fps = ((frames_now - frames_1s_ago) * 1000U) / elapsed_ms;
bsp_lcd_draw_text(4U, 4U, "FPS:", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
bsp_lcd_draw_text_uint(4U + 4U*LCD_FONT_W, 4U, fps, 3U, LCD_COLOR_GREEN, LCD_COLOR_BLACK);
```

### ⚠️ 这里有个 HAL 的坑（重要）

**不能直接用 `HAL_LTDC_IRQHandler()` 做帧计数！** 看 HAL 源码：

```c
/* stm32h7xx_hal_ltdc.c → HAL_LTDC_IRQHandler() */
/* Line Interrupt management */
if (((isrflags & LTDC_ISR_LIF) != 0U) && ((itsources & LTDC_IER_LIE) != 0U))
{
    __HAL_LTDC_DISABLE_IT(hltdc, LTDC_IT_LI);   /* ← 处理完就关掉行中断 */
    __HAL_LTDC_CLEAR_FLAG(hltdc, LTDC_FLAG_LI);
    ...
}
```

**HAL 会在第一次行中断后关闭 `LIE`**，所以用它只能数到 **1 帧**就停了。
本工程改成**自包含的最小中断处理**（只清标志 + 计数，不动使能位）：

```c
void LTDC_IRQHandler(void)
{
    if ((LTDC->ISR & LTDC_ISR_LIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CLIF;   /* 只清标志 */
        s_lcd_frames++;              /* 使能位保持，每帧继续触发 */
    }
}
```

> **注意**：如果以后在 CubeMX 里使能了 LTDC 全局中断，CubeMX 会在 `stm32h7xx_it.c`
> 里生成自己的 `LTDC_IRQHandler` → **链接会报重复符号**，此时把 `bsp_lcd.c` 里的删掉即可。

### 预期效果

左上角显示 `FPS:060`（白字标签 + 绿字数字），串口每秒打印一行 `[fps] 60`。
理论值 60.1，实测应在 **59~60** 之间。

---

## 6. CubeMX 使用规则（防止改的代码被覆盖）

| 位置 | 重新生成代码时 | 建议 |
|---|---|---|
| `BSP\Src\bsp_*.c`、`BSP\Inc\bsp_*.h` | ✅ **完全不动** | **首选在这里写应用代码** |
| `Core\Src\*.c` 的 `/* USER CODE BEGIN x */` … `END x` 之间 | ✅ 保留 | 必须改生成文件时只写在这里面 |
| `Core\Src\*.c` 的其它区域 | ❌ **会被覆盖** | 不要改（本次 `ltdc.c` 的 PixelFormat 是例外，必须同步到 CubeMX 里改） |
| `Drivers\`（CMSIS/HAL） | ❌ 不动 | 不要改 |
| `MDK-ARM\LED.uvprojx` | ⚠️ 会重新生成 | 新增 .c 文件后可能丢失，尽量复用已有文件 |
| `LED.ioc` | — | 时钟/引脚/外设参数都在这里配 |

**重新生成前的检查**（Project Manager 页）：
```
☑ Generate peripheral initialization as a pair of '.c/.h' files
☐ Delete previously generated files when not re-generated   ← 千万别勾！
```
