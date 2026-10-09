# STM32H743 + LVGL 9.3 移植说明

> 面向本工程（7 寸 800×480 18 位 RGB666 面板 + W9825G6KH-6 32MB SDRAM）
> 本次工作：**精简重写屏幕/SDRAM 驱动 → `Hardware/`**、**接入 LVGL 9.3.0**、**改好 Keil 工程**、**`main.c` 接好并给了一个能跑的界面**。
>
> 编译状态：**0 Error, 0 Warning**（Keil MDK Plus 5.42 / ARMCC 5.06u5 / 目标 STM32H743IITx）

---

## 0. 先看这三件事

1. **上电顺序不能乱**，尤其是「GT911 初始化必须在开背光之前」，见第 4 章。
2. **CubeMX 的 `.ioc` 已经一起改好了**（LTDC 回调、MPU 分区、栈大小），
   用 CubeMX 重新生成代码不会再被改回去，见第 5.3 节。
   其中最要紧的一条是 **栈从 1KB 加到了 8KB**（原来的 1KB 会让 LVGL 偶发 HardFault），见第 5.4 节。
3. **`BSP/` 文件夹已不再参与编译**，可以整个删掉（里面的诊断笔记建议留下来），见第 9 章。

---

## 1. 目录结构

```
STM32H7_LCD/
├── Hardware/                    ★ 新增：外设驱动（CubeMX 不生成，随便改）
│   ├── lcd.c / lcd.h            LTDC + DMA2D 屏幕驱动
│   ├── sdram.c / sdram.h        FMC SDRAM 驱动 + 全片自检
│   ├── gt911.c / gt911.h        GT911 电容触摸
│   └── i2c_bus.c / i2c_bus.h    I2C2 统一读写
│
├── Component/LVGL/            ★ 新增：LVGL 9.3.0
│   ├── lv_conf.h                LVGL 配置（只写需要改的项，其余用官方默认值）
│   ├── lvgl/                    LVGL 9.3.0 官方源码（未改动，仅补了 3 个文件末尾换行）
│   ├── port/                    ★ 移植层
│   │   ├── lv_port.c / .h       tick 时钟 + 串口日志 + 总入口 lv_port_init()
│   │   ├── lv_port_disp.c       显示：双缓冲直接渲染 + 硬件垂直消隐换页
│   │   └── lv_port_indev.c      输入：GT911 触摸
│   └── lvgl_sources.txt         实际参与编译的 204 个 LVGL 源文件清单
│
├── Core/Src/main.c              ★ 改了 MPU 配置 + 初始化顺序 + 一个示例界面
├── Core/Inc/stm32h7xx_hal_conf.h  ★ 改了 1 个宏（见 5.3）
├── Project.ioc                  ★ 同步改了 LTDC 回调 / MPU 分区 / 栈大小（见 5.3）
├── MDK-ARM/startup_stm32h743xx.s  ★ 栈 0x400 → 0x2000（见 5.4）
├── UI/                          ★ 界面与图片资源
│   ├── ui.c / ui.h              两个界面（背景图+灯图标 / 亮度滑条）+ 切换动画
│   ├── img_background.c/.h      背景图 800x480 RGB565（自动生成）
│   ├── img_light.c/.h           灯图标 64x64 RGB565 白底（自动生成）
│   └── gen_images.py            从 image/ 下的 PNG 生成上面两个文件
├── image/                       ★ 你的原始 PNG（background1 / light / LED）
│
├── MDK-ARM/Project.uvprojx      ★ 加了 19 个分组 / 253 个文件 / 6 条头文件路径
│   └── Project.uvprojx.bak      改动前的原始工程备份
│
└── BSP/                         ⚠️ 原驱动，已不参与编译，可删（建议留笔记）
```

---

## 2. BSP → Hardware 对照：改了什么、删了什么

### 2.1 文件对照

| 原文件（BSP/） | 新文件（Hardware/） | 变化 |
|---|---|---|
| `Src/bsp_lcd.c` + `Inc/bsp_lcd.h` | `lcd.c` + `lcd.h` | `bsp_lcd.c` **393 行** → `lcd.c` **75 行** |
| `Src/bsp_sdram.c` + `Inc/bsp_sdram.h` | `sdram.c` + `sdram.h` | `bsp_sdram.c` **406 行** → `sdram.c` **274 行** |
| `Src/bsp_gt911.c` + `Inc/bsp_gt911.h` | `gt911.c` + `gt911.h` | 复位时序不变（已验证可用），只清理风格 |
| `Src/bsp_i2c.c` + `Inc/bsp_i2c.h` | `i2c_bus.c` + `i2c_bus.h` | 只保留 GT911 需要的 16 位寄存器读写 |

### 2.2 屏幕驱动里删掉的东西（以及为什么可以删）

| 删掉的 | 原因 |
|---|---|
| 8×12 ASCII 字库 `font8x12[95][12]`（1140 字节 Flash） | LVGL 自带完整字体系统（Montserrat 多字号），字库没有任何保留价值 |
| `bsp_lcd_draw_char / _text / _text_width / _text_uint` | 同上，交给 LVGL 的 `lv_label` |
| `bsp_lcd_draw_pixel / _hline / _vline / _rect`、Bresenham `_draw_line` | LVGL 的绘图引擎全都有，而且带裁剪、抗锯齿、混合，自己写的只会更慢 |
| `bsp_lcd_test_pattern()`（八色竖条） | 上电直接显示 LVGL 界面，彩条测试不再需要 |
| `bsp_lcd_backlight(on)`（只有开/关） | 换成 **PWM 调光**：`lcd_backlight_init()` + `lcd_backlight_set(0~100)`，见 3.2 |
| `bsp_lcd_fill_rect()` 的**局部**填充能力 | 只保留整屏 `lcd_fill()`：整屏填充的 `OutputOffset` 恒为 0，于是**彻底绕开**「`HAL_DMA2D_Start()` 不重载 OOR 寄存器 → 局部填充每行连成一片」那个坑（原笔记 Bug 2/4）。需要任意矩形就交给 LVGL |
| `LTDC_IRQHandler` 里的手工帧计数 `s_lcd_frames` | 改用 LVGL 官方 `lv_st_ltdc` 的重载中断机制，一个中断同时解决「切页同步」和「帧率统计」。要看 FPS：打开 `LV_USE_SYSMON` + `LV_USE_PERF_MONITOR`（见 7.3） |

### 2.3 SDRAM 驱动里删掉/简化的东西

| 变化 | 说明 |
|---|---|
| **删掉 45 行手写的 `sdram_cache_sync()`**（直接操作 `SCB->DCISW/DCCSW/CCSIDR` + 超时保护） | 换成 4 行 CMSIS 标准写法：`if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) { SCB_CleanInvalidateDCache(); }`。原来那 45 行就是为了绕开「D-Cache 没使能时死等维护完成标志」的坑，现在一句「先判断再调用」就解决了 |
| **walking-1 ×32 轮从「快速自检」里移除** | 它是最耗时的一项（32 遍全片读写）。现在只放在 `sdram_test_full()` 里，`sdram_test_quick()` 快很多 |
| 删掉 `bsp_sdram_fill()` | 没人用；`memset` 就行（SDRAM 非缓存，不需要特殊处理） |
| 报告结构体去掉 `phase` / `words_tested` 两个字段 | `phase` 和返回值重复（失败即返回），`words_tested` 调用方自己知道 |

**保留的东西**：JEDEC 上电序列（`CLK_ENABLE → PALL → AUTOREFRESH×8 → LOAD_MODE`）、刷新率设置、五阶段自检（数据总线 / 地址总线 / 0x55AA / 伪随机 / 全片 walking-1）、进度日志钩子 `g_sdram_log`、错误现场 `g_sdram_report`（Keil 里可直接 watch）。

---

## 3. 屏幕：显示刷新、背光调光与界面

### 3.1 显示方案：为什么画面不撕裂

用的是 **LVGL 官方自带的 ST LTDC 驱动**（`lvgl/src/drivers/display/st_ltdc/lv_st_ltdc.c`），我们只写 5 行调用它：

```c
disp = lv_st_ltdc_create_direct((void *)LCD_FB0_ADDR, (void *)LCD_FB1_ADDR, 0U);
```

它内部做的事情正好就是我们要的：

1. **直接渲染模式（`LV_DISPLAY_RENDER_MODE_DIRECT`）**
   LVGL 直接渲染进 LTDC 的显存，**一次 memcpy 都不需要**。
2. **双缓冲**
   两块显存就是 SDRAM 最前面的 3MB（各 1.5MB）：LVGL 往「后台」那块渲染，LTDC 正在扫描「前台」那块。
3. **硬件垂直消隐换页** —— 不撕裂的关键
   一帧渲染完，驱动调用：
   ```c
   HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)px_map, 0);   // 写影子寄存器
   HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);      // 请硬件在消隐期生效
   ```
   LTDC 在**垂直消隐期**自动把新地址搬进活动寄存器，换页完成的 `RRIF` 中断再通知驱动。
   所以既不需要轮询、也不会在扫描到一半时换地址。
4. **两块显存自动保持一致**
   双缓冲直接渲染有个经典问题：本帧只重画了"改动区域"，另一块缓冲里那片区域还是两帧前的内容。
   LVGL 9.3 的 `lv_refr.c` 自己会记 `sync_areas` 并把改动区域同步到另一块，不用我们管。

> 想换成三缓冲（更抗抖动）？把 `lv_port_disp.c` 里加一个第三块缓冲即可，SDRAM 32MB 完全放得下。

### 3.2 背光亮度控制（PWM 调光）

#### 硬件

```
PB0 --R64(0Ω)--> U3 MT9284-28J 的 EN(CE) 脚
```

U3 是**升压型**白光 LED 驱动（1.2MHz 固定频率、SOT23-6、输入 2.5~5.5V），
它的 CE 脚既是使能、也是 **PWM 调光输入** —— 该型号规格里明确写了支持 PWM 与模拟两种调光
（[HQ Online 器件属性](https://www.hqonline.com/product-detail/led-lighting-drivers-aerosemi-mt9284-28j-2500374726)、
[电子发烧友介绍](https://m.elecfans.com/article/2087578.html)）。

原来 PB0 只是个普通 GPIO，只能开/关。要做 0~100 的亮度就必须让它输出 PWM。

#### 底层（`Hardware/lcd.h` + `lcd.c`）

原来只有一个开关函数：

```c
void lcd_backlight(uint8_t on);      /* 只有开/关，GPIO 直推 */
```

改成了两个：

```c
void lcd_backlight_init(void);            /* 配好 TIM3_CH3(PB0) 的 PWM，占空比 0 = 灭 */
void lcd_backlight_set(uint8_t percent);  /* 0~100 调亮度：0 = 全灭，100 = 最亮 */
```

> ⚠️ **注意语义变了**：老代码里 `lcd_backlight(1U)` 表示"开"。
> 现在没有这个函数了，要开就是 `lcd_backlight_set(100U)`。
> `main.c` 里的调用已经同步改过，别在别处再写 `lcd_backlight(1U)`。

实现要点：

| 项 | 值 | 说明 |
|---|---|---|
| 定时器 / 通道 | **TIM3_CH3** | PB0 的 AF2。同一根引脚还能走 TIM1_CH2N(AF1) / TIM8_CH2N(AF3) |
| 计数器时钟 | 240 MHz | HCLK=240MHz、APB1 分频=2 → PCLK1=120MHz，定时器时钟再 ×2 |
| `PSC` / `ARR` | `119` / `999` | 240MHz ÷ 120 ÷ 1000 = **2 kHz** |
| 占空比级数 | 1000 级 | 1% 亮度 = 10 级，滑条从 0 拖到 100 是平滑的 |
| **0%** | `CCR = 0` | 输出常低 → MT9284 关断（<1µA），背光全灭 |
| **100%** | `CCR = 1000 > ARR` | 输出常高，**完全没有开关动作** → 最亮档位一点啸叫都没有 |

`lcd_backlight_init()` 由 `lcd_init()` 自动调用，而且**配好后占空比是 0**，
所以"背光在 `gt911_init()` 之前必须是灭的"这个上电顺序约束依然成立。

⚠️ `MX_GPIO_Init()` 会把 PB0 配成普通推挽输出，`lcd_backlight_init()` 里会把它
**重新配成复用功能** —— 这一步不能省，否则引脚被 GPIO 输出占着，PWM 出不来。

#### 顶层（`Core/Src/main.c`）

界面换成一个 0~100 的滑条：

```c
static void bl_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t   value  = lv_slider_get_value(slider);

    lcd_backlight_set((uint8_t)value);              /* 滑条值直接当百分比用，不做换算 */
    lv_label_set_text_fmt(s_label_bl, "Brightness: %d", (int)value);
}
```

- 范围：`lv_slider_set_range(slider, 0, 100)`，初始值 100（最亮）
- 事件：`LV_EVENT_VALUE_CHANGED`（拖动过程中就会触发，是实时跟手的）
- 取值：`lv_slider_get_value()`
- 滑条的 0~100 和 `lcd_backlight_set()` 的入参含义完全一致，中间不做任何映射

#### ⚠️ PWM 频率怎么选（唯一需要现场调的地方）

MT9284 是**升压**拓扑，EN 关断期间靠输出电容维持 LED 电流。
频率越高，每个开关周期越来不及重新建立电流调节：

| 频率 | 效果 |
|---|---|
| 200 ~ 500 Hz | 线性最好，但可能在陶瓷电容 / 电感上听到轻微啸叫 |
| **2 kHz（默认）** | 折中 |
| 10k ~ 20 kHz | 听不见，但低亮度区间可能不线性、甚至闪 |

要改就改 `Hardware/lcd.h` 里的一个宏：

```c
#define LCD_BL_PWM_HZ       2000U      /* 有啸叫就调高；低亮度不线性/闪就调低 */
```

**我没法上板验证**，这个 2kHz 是按"升压拓扑 + 常见背光调光实践"取的折中值。
第一次烧进去请重点看两件事：

1. **低亮度（5%~20%）会不会闪、或者突然灭** → 会就把频率调低（试 500Hz）
2. **会不会听到啸叫** → 会就把频率调高（试 10kHz 以上，同时确认低亮度还线性）

如果调到很低百分比时背光干脆不亮（升压启动需要最小导通时间），
在滑条回调里把非零的最小值抬起来即可：

```c
if (value > 0 && value < 5) { value = 5; }    /* 最小 5%，避开点不亮的区间 */
```

### 3.3 界面与图片资源（背景图 + 灯图标 + 浮层面板）

#### 文件

```
UI/
  ui.c / ui.h            两个界面的代码
  img_background.c/.h    背景图  800x480  RGB565     数据 768,000 B
  img_light.c/.h         灯图标   64x64   RGB565白底 数据   8,192 B
  gen_images.py          从 image/ 下的 PNG 生成上面两个文件
  preview.png            两个界面的排版示意图（见下）
image/                   你的原始 PNG（background1.png / light.png / LED.png）
```

> 上板前想先看排版：打开 **`UI/preview.png`**。那是按 `ui.c` 里的布局参数
> 用 Pillow 预先渲染的示意图 —— 位置、尺寸、层次是准的，
> **但字体用的是系统字体，不是 LVGL 的 Montserrat，字形会有差异**。

#### 图片是怎么变成 C 数组的

LVGL 在 MCU 上显示图片的标准做法：一个 `lv_image_dsc_t` 描述符 + 一段像素数组。
本工程没用在线转换器，而是用 `UI/gen_images.py`（Python + Pillow）离线生成，
好处是**换图只要替换 PNG 再跑一次脚本**：

```powershell
& "<python路径>" UI\gen_images.py
```

| 图 | 源文件 | 输出 | 说明 |
|---|---|---|---|
| 背景 | `image/background1.png`（1920×1080 照片） | 800×480 **RGB565** | 先等比放大到「铺满 800×480」再居中裁掉左右多出来的部分 —— 不变形、不留黑边。RGB565 是 2 字节/像素，照片在 7 寸屏上看不出差别 |
| 灯图标 | `image/light.png`（200×200 带透明通道） | 64×64 **RGB565**（白底） | 缩到 64（和电脑桌面快捷方式图标差不多大），并把**透明背景合成成白色**。合成后完全不透明，就不需要 alpha 通道了，用 RGB565 存只有 8,192 B（ARGB8888 要 16,384 B） |

打包细节（都实测验证过）：

- **RGB565**：2 个像素打包进 1 个 `uint32`。小端 MCU 上 `uint32` 的低 16 位排在内存前面，
  正好等价于连续的 `uint16` 数组 —— 就是 LVGL 要的字节序
- **白底图标**：`gen_images.py` 里用 `Image.alpha_composite(白底, 原图)` 把透明部分填成白色，再转 RGB565。
  这样图标是个不透明白色方块，正好是"桌面图标"的样子，也省掉 alpha 通道。
  想改回透明背景：把脚本里的 `ICON_WHITE_BG` 改成 `False`，它会自动改用 ARGB8888 输出
- 都声明成 `const uint32_t[]`，落在 Flash 的 `.constdata`，并且自然 4 字节对齐
  （满足 LVGL 的 `LV_DRAW_BUF_ALIGN` = 4）
- 描述符里的 `magic = LV_IMAGE_HEADER_MAGIC`（0x19）**是关键**：
  `lv_image_src_get_type()` 只看第一字节 —— 0x20~0x7F 当文件路径、≥0x80 当符号、
  小于 0x20 才当图像描述符。0x19 正好落在这个区间，所以能被正确识别

> **数据对不对是怎么确认的**：把生成的 C 数组反解回图片，和 Pillow 处理出的预期图逐像素比对 ——
> 背景最大通道误差 = 7，正好是 RGB565 的量化极限（符合预期，说明没有任何字节错位）；
> 图标 **逐像素完全一致**。没法上板的时候，这是能拿到的最强证据。

#### 一个屏幕 + 浮层面板（★ 渲染方案）

**只有一个屏幕。**按下左上角的图标时**不切屏、不重画背景**，
而是把一块亮度面板当浮层显示出来：

```
面板收起（上电状态）              按下图标 ->          面板打开
┌────────────────────────┐                          ┌────────────────────────┐
│ [图标]                  │                          │ [图标]        ┌──────┐ │
│ Brightness              │                          │ Brightness    │  ×   │ │
│                        │                          │  ┌────────────┴──────┐ │
│  背景图（铺满整屏）      │                          │  │   Brightness     │ │
│                        │                          │  │      100 %       │ │
│                        │                          │  │   ●────────────  │ │
│                        │                          │  └──────────────────┘ │
└────────────────────────┘                          └────────────────────────┘
                                                      按 × 收起（再滑回去）
```

`UI_PANEL_ANIM_MS = 180`，面板从下方 24px 滑到位，用 `lv_anim` 改 `translate_y`。

**为什么这样省 —— 两个关键点：**

**① 面板做成了「不透明 + 直角」，让 LVGL 跳过它底下的背景**

LVGL 每次重绘前会做一次"覆盖检查"（`LV_EVENT_COVER_CHECK`）：
从最上层往下找，如果某个对象**完全不透明**且**完全盖住**了这次要重绘的区域，
那它下面的东西就都不用画了。

`lv_obj.c` 里判定"盖住"的条件（`lv_obj_draw()` 的 `LV_EVENT_COVER_CHECK` 分支）：

| 条件 | 本工程 |
|---|---|
| `radius == 0`（四角要落在矩形内） | ✅ `lv_obj_set_style_radius(panel, 0, ...)` |
| `bg_opa >= LV_OPA_MAX` | ✅ `LV_OPA_COVER` |
| 对象 `opa >= LV_OPA_MAX` | ✅ 没设过 |
| 不能有半透明的渐变 | ✅ 没有 |

> ⚠️ **这一条就是"别改成圆角/半透明"的原因**：只要 `radius` 不是 0，
> 四角落在圆角外面，检查立刻失败，每次重绘面板都得把底下的背景**重新画一遍**。
> 想要圆角外观就得接受这个代价（见下面的对比）。

**② 面板只占 620×250（全屏的 40%）**

滑入/收起时每帧只有两小块要重绘：面板本身（不透明，背景被跳过）
+ 它挪开后露出来的那条 620×24 的背景。

**和上一版方案（两个屏幕 + `lv_screen_load_anim` 滑动切换）的对比：**

| | 两个屏幕 + 切屏动画 | 一个屏幕 + 浮层面板（现在） |
|---|---|---|
| 每动画帧重绘面积 | 整屏 ×2（两个屏幕都在动） | 面板 + 一条 620×24 的边 |
| 背景图（RGB565→ARGB8888）转换 | 每帧全屏 38 万像素 | **只有露出的那一条** |
| 双缓冲同步拷贝 | 每帧全屏 | 只有面板那块 |
| 静态时（不动） | — | **完全零开销**（LVGL 按需渲染，没 invalidate 就不画） |

#### 几个容易踩的点（代码里都处理了）

| 点 | 为什么 |
|---|---|
| 屏幕用 `lv_obj_create(NULL)` 新建，并**去掉滚动/内边距/边框** | 否则背景图会被内边距挤偏，或者整屏能被手指拖走 |
| 背景图 `lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE)` | 背景图铺满整屏，是可点击对象的话会把灯图标的点击吃掉 |
| 入口放左上角 `lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 16, 16)`，名字用 `lv_obj_align_to(label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 4)` 挂到图标下面 | 就是"桌面快捷方式"的摆法。改位置/间距只需要动这两个数 |
| 图标下面的名字给白底 + 深色字 | 背景是照片，深浅不可控；深色字直接写上去某些区域会看不清 |
| 面板上电先 `lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN)` | 隐藏的对象完全不参与绘制 |
| 收起动画放完才真正 `HIDDEN`（`lv_anim_set_completed_cb`） | 直接隐藏就没动画了 |
| `lv_anim_delete(panel, ...)` 放在开新动画之前 | 快速连点图标/× 时，两个动画会抢同一个 `translate_y` |
| × 号用 `LV_SYMBOL_CLOSE` + `LV_RADIUS_CIRCLE` 圆钮 | 内置 Montserrat 字体自带符号区（cmap 覆盖 `176 ~ 63651`，含 `0xF00D`），不用额外加字体 |
| `lv_obj_set_style_shadow_width(close, 0, ...)` | 主题给按钮默认加了阴影，圆钮上会显脏 |
| 图标按下反馈 `lv_obj_set_style_opa(icon, LV_OPA_70, LV_STATE_PRESSED)` | 图片本身没有按下态，不加这个按下去没反应 |
| `ui_init()` 里**不设背光亮度** | 背光必须等 `gt911_init()` 之后由 `main()` 开，否则面板复位会当众闪一下 |

> 图标只有 64×64，要是觉得手指不好点，两个办法：
> 把 `UI/gen_images.py` 的 `ICON_SIZE` 调大重跑脚本；
> 或者把 `light_event_cb` 也挂到下面那行文字上（`ui.c` 里留了注释）。

#### 渲染间隔：33ms -> 16ms（帧率上限 30 -> 62）

`lv_conf.h`：

```c
#define LV_DEF_REFR_PERIOD 16      /* 官方默认 33 */
```

LVGL 9 的刷新是**按需触发**的：有东西需要重绘（invalidate）时把刷新定时器唤醒，
到点渲染一帧，然后定时器自己 `lv_timer_pause()`；下次被 invalidate 再唤醒
（`lv_display.c:1344` 那句 `lv_timer_resume(disp->refr_timer)`）。

而 **LVGL 源码里没有任何地方会修改这个定时器的周期** —— 整个工程只有
`lv_ffmpeg.c` / `lv_monkey.c` 调过 `lv_timer_set_period`，这两个都没编进来。
所以它就是**固定的渲染间隔**：

```
帧率上限 = 1000 / LV_DEF_REFR_PERIOD
```

| 值 | 帧率上限 | 说明 |
|---|---|---|
| 33（官方默认） | **30 fps** | 对 60Hz 的 LTDC 来说白白浪费一半 |
| **16（现在）** | **62 fps** | 正好对上 800x480@60Hz 的屏 |
| 10 | 100 fps | 没意义，屏本身只有 60Hz，只会白耗 CPU |

> 出处：`lv_display.c:108` 用 `LV_DEF_REFR_PERIOD` 创建刷新定时器。

#### ⚠️ 滑条拖到 0 会全黑

0~100 是你要的范围，但 **0% 就是背光全灭、屏幕全黑**，看着像死机。
触摸并没有停，在滑条原来的位置往右拖还能调回来，只是暂时看不见。

想彻底避免，在 `UI/ui.c` 的 `slider_event_cb()` 里加一行垫底：

```c
if (value > 0 && value < 5) { value = 5; }     /* 最低 5%，永远看得见 */
```

---

## 4. 上电顺序（★ 照抄即可）

`main.c` 里 `USER CODE BEGIN 2` 区已经是这个顺序，**不要调整**：

```c
sdram_init();          // 1) SDRAM 上电序列（必须）
sdram_test_quick();    //    可选自检。会覆盖整个 SDRAM，必须在下面之前做！

lcd_init();            // 2) 配 DMA2D、开 LTDC 换页中断、配好背光 PWM（占空比 0，仍关着）、清黑显存

lv_port_init();        // 3) lv_init() + tick + 显示 + 触摸
ui_init();             //    画第一帧（背景图 + 左上角入口，亮度面板是收起的）—— 背光关着，看不见
lv_timer_handler();

gt911_init();          // 4) 触摸初始化。这一步会把面板一起复位（见下）

lcd_backlight_set(100U);  // 5) 所有复位动作都做完了，最后才开背光 → 不闪屏
                          //    100 = 最亮；之后拖动滑条会实时改这个亮度
```

### 为什么背光必须最后开

```
PG14 = LCD_TP_RST  —— 触摸复位
PG14 = 面板 P_RESET —— FPC 第 31 脚
```

**这两根是同一根线。** `gt911_init()` 要把 PG14 拉低 20ms 复位 GT911，
面板同时也被复位 → 画面黑掉约 20ms 再恢复。
如果这时背光已经开着，用户就会看到屏幕「闪一下」（原笔记 Bug 7）。

**通用原则：所有面板/触摸复位动作都要在背光关闭期间完成。**

### 自检开关

`main.c` 的 `USER CODE BEGIN PD` 区：

```c
#define SDRAM_SELFTEST_EN   1   // 1 = 每次上电做快速自检（1 秒内）; 0 = 跳过，启动更快
```

调新板子设 `1`，板子验证过之后设 `0`。
**无论开关怎么设，`sdram_init()`（JEDEC 上电序列）都必须保留**，否则 SDRAM 根本不工作。

---

## 5. Keil 工程改了什么

### 5.1 头文件搜索路径（新增 6 条）

```
../Hardware
../Component/LVGL
../Component/LVGL/lvgl
../Component/LVGL/lvgl/src/drivers/display/st_ltdc
../Component/LVGL/port
../UI
```

> `../UI` 是给 `main.c` 用的（它要 `#include "ui.h"`）。
> `ui.c` 里 include `img_background.h` 用的是引号包含，会先找同目录，所以不依赖路径。
>
> 没有加 `LV_CONF_INCLUDE_SIMPLE` 之类的宏：`lv_conf.h` 就放在 `lvgl/` 文件夹旁边，
> LVGL 默认按 `../../lv_conf.h` 找，正好命中。

### 5.2 文件分组（新增 19 组 / 214 个文件）

```
UI                          3     ui.c img_background.c img_light.c
Hardware                    4     lcd.c sdram.c gt911.c i2c_bus.c
Component/LVGL/port       3     lv_port.c lv_port_disp.c lv_port_indev.c
Component/LVGL           至 47   按 LVGL 的 src 子目录分了 15 组
                                  (core/display/draw/font/indev/layouts/misc/
                                   osal/others/stdlib/themes/tick/widgets/
                                   drivers/libs)
```

**只编译了 204 个 LVGL 源文件**，不是全部 1100+ 个。被排除的是：

| 排除目录 | 原因 |
|---|---|
| `src/libs/*`（除 `bin_decoder`） | 第三方图片/矢量库（lodepng、libpng、tiny_ttf、SVG、thorvg…）全部没开 |
| `src/drivers/*`（除 `st_ltdc`） | 各种屏/触摸 IC 的官方驱动，我们用 `Hardware/` 里的自研驱动 |
| `src/draw/{dma2d,nema_gfx,nxp,opengles,renesas,sdl,vg_lite}` | GPU/硬件加速后端，全部没开 |
| `src/others/{xml,test,monkey,snapshot,file_explorer,font_manager,imgfont,ime,gridnav,fragment}` | 可选功能模块，没开 |
| `demos/ tests/ examples/ docs/ xmls/ zephyr/ scripts/` 等 | 复制时就没拷（省 6MB，也不参与编译） |

> `src/libs/bin_decoder/lv_bin_decoder.c` **必须留着** —— `lv_init()` 会无条件调用
> `lv_bin_decoder_init()`，这是 LVGL 内置的二进制图片解码器。
>
> 以后要开某个被排除的模块（比如 PNG 解码）：把对应目录加进 Keil 分组，
> 同时把 `lv_conf.h` 里的 `LV_USE_LIBPNG` 改成 1 即可，源码都在磁盘上。

### 5.3 ⚠️ CubeMX 相关的改动（**已经写进 `Project.ioc`，重新生成代码也不会被改回去**）

工程根目录有 `Project.ioc`，说明这个工程是可以用 CubeMX 重新生成代码的。
下面这些设置**同时改了生成出来的代码和 `.ioc`**，所以两边是一致的：

| 设置 | 改前 | 改后 | 为什么 |
|---|---|---|---|
| **LTDC Register Callbacks** | 关（`USE_HAL_LTDC_REGISTER_CALLBACKS = 0U`） | **开（`= 1U`）** | `lv_st_ltdc` 要注册「LTDC 重载完成」回调才知道换页何时完成；`HAL_LTDC_RegisterCallback()` 只有在为 1 时才存在，不为 1 直接编译报未定义符号 |
| **MPU Region1**（SDRAM 32MB） | `Cacheable + Bufferable` | **`Non-cacheable + Bufferable`** | CPU 走 Cache、LTDC/DMA2D 直连 SDRAM 颗粒，视图必须一致，否则花屏（原笔记 Bug 5） |
| **MPU Region2** | 基址 `0xC0000000` / 4MB（和 Region1 **同基址重叠**！） | **基址 `0x24000000`（AXI SRAM）/ 512KB / Cacheable + Bufferable** | 重叠区域的优先级行为不可靠，正是「显存仍被缓存」的根因 |
| **栈大小** `StackSize` | `0x400`（**1KB**） | **`0x2000`（8KB）** | 见第 5.4 节 |

对应生成位置：
- `Core/Inc/stm32h7xx_hal_conf.h` 第 200 行 `USE_HAL_LTDC_REGISTER_CALLBACKS`
- `Core/Src/main.c` 的 `MPU_Config()`
- `MDK-ARM/startup_stm32h743xx.s` 第 32 行 `Stack_Size`

> 也就是说：**用 CubeMX 重新生成代码后，这些设置会保持正确**，不需要每次手动改回来。
> 唯一要留意的是 `ProjectManager.DeletePrevious`（CubeMX 里是
> 「Delete previously generated files when not re-generated」）—— 你的笔记里提醒过
> 「千万别勾」，如果哪天发现 `Core/` 里少了文件，先看这个选项。

### 5.4 栈大小：1KB → 8KB（很重要）

`MDK-ARM/startup_stm32h743xx.s`：

```asm
Stack_Size      EQU     0x2000      ; 原来是 0x400（只有 1KB）
```

**为什么必须加**：LVGL 9 的软件光栅化器在渲染时有嵌套调用
（`lv_draw_sw` → 图层/遮罩 → 字形渲染，`lv_snprintf` 内部也有不小的局部数组），
1KB 的栈很容易溢出，表现是**偶发 HardFault**、画面画到一半死掉，而且极难定位。
8KB 放在 DTCM（共 128KB）里完全放得下。

`Project.ioc` 里的 `ProjectManager.StackSize` 也同步改成了 `0x2000`。

### 5.5 编译警告

LVGL 自己的代码在 ARMCC 5 下会报约 360 个风格类警告（枚举类型混用 `#188`、
文件末尾无换行 `#1`、goto 跳过初始化 `#546` 等），纯属上游代码风格，不影响功能。

处理方式：**只对 `Component/LVGL/*` 分组**加了 `--diag_suppress=1,68,111,188,191,546,1293`，
`Hardware/`、`port/` 里的自家代码保持**全警告打开**，免得真问题被淹掉。

> 想恢复看全部警告：在 Keil 里右键 `Component/LVGL` 下的任一分组 →
> `Options for Group` → `C/C++` → 把 `Misc Controls` 里的 `--diag_suppress=...` 删掉；
> 如果警告没回来，再把这个分组的选项整体恢复成「继承目标设置」（对话框里的 Reset / 默认）。
>
> 附带说明：Keil 保存工程时会把分组的选项补全成一整块 `<GroupOption>` 写回 `.uvprojx`
> （把没显式设的项填成「继承」），所以你现在用文本编辑器打开 `.uvprojx` 会看到
> 每个 LVGL 分组下面都有一大段选项——**这是正常的，不用管**。

### 5.6 编译器

工程仍然用 **ARM Compiler 5（V5.06 update 5）**，**不需要**换成 AC6。
全部 204 个 LVGL 源文件已逐个用 armcc 试编译验证通过。

---

## 6. 编译结果

```
Program Size: Code=285994  RO-data=874238  RW-data=264  ZI-data=75432
"Project\Project.axf" - 0 Error(s), 0 Warning(s)
Build Time Elapsed: 00:02:57
```

| 项目 | 占用 | 说明 |
|---|---|---|
| Flash | 约 **1.11 MB** / 2 MB | Code + RO-data + RW-data；其中 **758 KB 是两张图片**（背景 768,000 B + 图标 8,192 B） |
| RAM | 约 **74 KB** | RW-data + ZI-data = 252 + 75428 |
| 全量编译时间 | 约 **3 分钟** | 改一个文件后是增量编译，很快 |

RAM 的构成：LVGL 内存池 `LV_MEM_SIZE` = 64KB、栈 8KB、其余为各种静态变量。
显存 3MB 不算在这里（它是直接指向 SDRAM 地址的指针，不占链接器 RAM）。

> 对照改造前：CubeMX 空壳工程是 `Code=14332 RO-data=752 RW-data=28 ZI-data=2188`。
> 也就是说整套 LVGL 9.3（204 个源文件、全部常用控件）只花了约 **350KB Flash**。

---

## 7. `lv_conf.h` 常用改动

文件位置：`Component/LVGL/lv_conf.h`。**只写了需要改的项**，其余全部由
`lvgl/src/lv_conf_internal.h` 给默认值 —— 这样 LVGL 升级时不会因为模板变化而报错。

### 7.1 想让界面显示中文

LVGL 自带的 Montserrat 字体**只有 ASCII**，中文必须自己生成：

1. 打开官方在线转换器 <https://lvgl.io/tools/fontconverter>
2. 选一个中文字体（比如思源黑体 / 微软雅黑），
   **Range 只填你真正用到的字**（例如 `0x4E00-0x4E20`），或直接在 Symbols 里粘贴你要用的那几十个字
   —— 中文字库体积和字数成正比，全字库有十几 MB，MCU 放不下
3. `Bpp` 选 4、`Format` 选 Binary 或 C array，导出 `.c` 文件
4. 把 `.c` 加进 Keil 的 `Hardware` 组，然后在 `lv_conf.h` 里声明：

```c
#define LV_FONT_CUSTOM_DECLARE   LV_FONT_DECLARE(lv_font_siyuan_20)
```

5. 用的时候：

```c
lv_obj_set_style_text_font(label, &lv_font_siyuan_20, LV_PART_MAIN);
```

### 7.2 想加控件

默认已经把全部常用控件打开了（`LV_USE_BUTTON`、`LABEL`、`CHART`、`TABLE`…）。
只有被排除出编译的模块需要「改 lv_conf.h + 往 Keil 里加文件」两步：

```c
#define LV_USE_XML           1      // 例如要开 XML 声明式 UI
```
然后往 Keil 的 `Component/LVGL/others` 组里加上 `src/others/xml/*.c`。

### 7.3 想看帧率 / 内存占用

```c
#define LV_USE_SYSMON       1
#define LV_USE_PERF_MONITOR 1      // 右下角显示 FPS + CPU 占用
#define LV_USE_MEM_MONITOR  1      // 显示 LVGL 内存池用量
```

> ⚠️ `lv_refr.c` 里有一句 `#if !LV_USE_PERF_MONITOR lv_timer_pause(tmr);` ——
> **打开性能监视器就关掉了按需渲染**，刷新定时器不再暂停，变成一直按固定周期重绘。
> 测完帧率记得关掉，否则白耗 CPU。

### 7.4 想再提速

```c
#define LV_USE_DRAW_DMA2D   1
```
打开后填充/混合走 DMA2D 硬件，但要把 `src/draw/dma2d/lv_draw_dma2d.c`
加进 Keil 分组，并且注意 `lv_st_ltdc` 的 `LV_ST_LTDC_USE_DMA2D_FLUSH`
与它**互斥**（只对 PARTIAL 模式有效，本工程用 DIRECT 模式，保持 0 即可）。

不打开 DMA2D 也能跑：SDRAM 是非缓存的，CPU 直接渲染，纯软件光栅化对这个分辨率够用。

### 7.5 帧率上不去 / 想调渲染间隔

```c
#define LV_DEF_REFR_PERIOD 16      /* 官方默认 33，等于锁死 30fps */
```

这一项是**固定的渲染间隔**（LVGL 不会自适应），`帧率上限 = 1000 / 这个值`。
本工程已经改成 16（上限约 62fps），对应 60Hz 的屏。详细解释见 **3.3 节**。

---

## 8. 排查手册

| 现象 | 先查什么 |
|---|---|
| **编译报未定义符号 `lv_bin_decoder_init`** | `src/libs/bin_decoder/lv_bin_decoder.c` 没加进工程 |
| **编译报 `HAL_LTDC_RegisterCallback` 未定义** | `USE_HAL_LTDC_REGISTER_CALLBACKS` 不是 `1U`（第 5.3 节）；若用 CubeMX 重新生成过代码，检查 `.ioc` 里 `ProjectManager.RegisterCallBack` 是否还是 `LTDC` |
| **画面画到一半死机 / 偶发 HardFault** | 先查栈大小。`startup_stm32h743xx.s` 的 `Stack_Size` 必须是 `0x2000`（LVGL 的软件渲染很吃栈，1KB 必爆） |
| **链接报 `LTDC_IRQHandler` 重复定义** | CubeMX 里勾了 LTDC global interrupt，把 `Hardware/lcd.c` 里的删掉（内容只有一行 `HAL_LTDC_IRQHandler(&hltdc);`） |
| **整屏缺红 / 颜色错乱** | LTDC Layer0 的 `PixelFormat` 必须是 `ARGB8888`（`Core/Src/ltdc.c`），且 DMA2D 必须是 `DMA2D_OUTPUT_ARGB8888`。两者名字不一样，配错就串色（原笔记 Bug 6） |
| **花屏 / 残影 / 画面抖动** | MPU 里 SDRAM 必须是 **Non-cacheable + Bufferable 整块 32MB**，且**不要**再叠一个同基址区域（`Core/Src/main.c` 的 `MPU_Config()`，原笔记 Bug 5） |
| **画面撕裂** | LTDC 全局中断没开，或 `LTDC_IRQHandler` 被别的文件抢了。`lcd_init()` 里会 `HAL_NVIC_EnableIRQ(LTDC_IRQn)` |
| **屏幕一直黑，但串口有输出** | 背光没开（`lcd_backlight_set(100U)` 要在 GT911 初始化之后调用），或 `LV_COLOR_DEPTH` 不是 32 |
| **滑条拖了但亮度不变** | ① `lcd_backlight_init()` 有没有跑（它由 `lcd_init()` 调用）；② PB0 有没有真的切成 AF2 —— `MX_GPIO_Init()` 会把它配成普通输出，必须被重新配成复用功能；③ 拖动时用示波器量 PB0，正常应该是 2kHz 方波 |
| **背景图/图标不显示，一片黑或纯色** | ① `UI/*.c` 有没有加进 Keil 的 UI 分组（尤其两张图的 .c）；② 串口有没有 LVGL 的 `Not supported source color format` 警告 —— 有的话说明 `LV_DRAW_SW_SUPPORT_RGB565` / `..._ARGB8888` 被关了（本工程用默认值 1，正常不会）；③ 图片描述符的 `header.magic` 必须是 `LV_IMAGE_HEADER_MAGIC`(0x19)，写错会被 LVGL 当成文件路径 |
| **背景图显示正常但图标点不动** | 背景图铺满整屏，如果没去掉它的 `LV_OBJ_FLAG_CLICKABLE`，点击会被背景吃掉。`UI/ui.c` 的 `add_background()` 里已经处理 |
| **按下灯图标没有切界面** | 确认用的是 `LV_EVENT_CLICKED`，并且图标加了 `LV_OBJ_FLAG_CLICKABLE`（`lv_image` 默认不可点击） |
| **弹出/收起面板时卡一下** | 面板被改成圆角或半透明了（`radius != 0` 或 `bg_opa < LV_OPA_COVER`）—— 覆盖检查会失败，于是每次重绘面板都要把底下的 RGB565 背景重画一遍。见 3.3 节的对比表 |
| **整体帧率上不去，感觉只有 30fps** | 检查 `lv_conf.h` 的 `LV_DEF_REFR_PERIOD` 是不是又被改回 33 了。这一项是**固定的**渲染间隔，不会自适应，33 就等于锁死 30fps（见 3.3 节） |
| **画面完全不动了（像死机但触摸还在响）** | LVGL 是按需渲染：没有 invalidate 就一帧都不画。如果自己写了动画/改控件却忘了触发重绘，画面就会静止。正常操作（改文本、动滑条）LVGL 会自动 invalidate |
| **换图之后显示花屏** | 改了 `image/` 下的 PNG 之后必须重跑 `UI/gen_images.py`，并且**确认新图和 `PANEL_W/PANEL_H` 对得上**；宽度必须是偶数（脚本会检查） |
| **触摸没反应** | 串口看有没有 `[main] GT911 not found`；是 0x5D 还是 0x14 由复位时 INT 电平决定，驱动两种都会试 |
| **触摸坐标不对/抖动** | `gt911_read()` 返回 1 且 `count==0` 才是「抬起」，返回 0 是「状态没变」。`lv_port_indev.c` 里就是按这个语义缓存状态的 |
| **卡在 `sdram_test_quick()` 不动** | D-Cache 相关的死等。本工程 SDRAM 非缓存 + D-Cache 关闭，不该出现；若手动改过 MPU/Cache 配置，见原笔记 Bug 1 |
| **LVGL 报 malloc 失败** | 加大 `lv_conf.h` 的 `LV_MEM_SIZE`（当前 64KB） |

---

## 9. 遗留事项 / 可以清理的东西

| 项 | 说明 |
|---|---|
| `BSP/` 文件夹 | **已不参与编译**（原来的 `Project.uvprojx` 里就没有它，所以一直没被编译过）。确认新驱动没问题后可以整个删掉；`BSP/LCD_移植与诊断笔记.md` 建议保留或挪到 `Hardware/`，那份笔记里记录的几个硬件坑仍然有效 |
| `MDK-ARM/Project.uvprojx.bak` | 改造前的原始工程备份，确认无误后可删 |
| `MDK-ARM/build_lvgl.log` | 本次最终编译日志（`0 Error(s), 0 Warning(s)`），可随时删 |
| `Component/LVGL/lvgl/` 里没参与编译的目录 | 比如 `src/libs/thorvg`（2MB）、`src/drivers/*`。留着方便以后开功能，嫌大可以删（删了要同步清 `lvgl_sources.txt` 里对应项，不过那些本来就不在编译列表里） |
| `lv_conf.h` 里的 `LV_LOG_LEVEL` | 现在是 `LV_LOG_LEVEL_WARN`，日志走 USART1（115200）。不想要串口日志就把 `LV_USE_LOG` 改成 0 |

---

## 10. 磁盘占用（为什么文件夹会变大）

### 10.0 界面与图片源码：约 2.4 MB

`UI/` 目录，其中绝大部分是自动生成的图片数据（**不要手改，也不要被吓到**）：

| 文件 | 大小 | 说明 |
|---|---|---|
| `img_background.c` | **2.04 MB** | 800×480 RGB565 的像素数据（768,000 字节写成 C 数组） |
| `img_light.c` | 0.02 MB | 64×64 RGB565 白底（8,192 字节） |
| `ui.c` / `ui.h` / `gen_images.py` | 约 9 KB | 手写的代码 |

为什么这么大：C 数组里每个字节要写成 `0x...` 的形式，大约 2.8 个字符换 1 个字节。
LVGL 官方在线转换器出来的文件也是这个量级，属于正常现象
（对比一下：LVGL 自带的中文字库 `lv_font_source_han_sans_sc_16_cjk.c` 也有 1.2 MB）。

> 编译这两个文件大约各花几秒，可以接受。
> 真嫌大的话，把 `UI/gen_images.py` 里的 `PANEL_W/PANEL_H` 调小、让 LVGL 运行时放大显示
> ——但放大是每帧都要做的运算，会更卡，不推荐。

`image/` 是你放原始 PNG 的地方（2.1 MB + 6 KB），换图时替换这里再跑脚本。

### 10.1 LVGL 源码：22.4 MB

`Component/LVGL/` 现在是 **22.4 MB / 1057 个文件**，就是 LVGL 9.3.0 的官方源码
（包含一部分没参与编译的模块）。

> ⚠️ 这里曾经一度是 **588 MB** —— 复制源码时把上游仓库的 **`.git` 目录一起拷了进来**
> （`.git/objects/pack/*.pack` 单个文件 565.9 MB）。**已经删掉了。**
> 同时删掉了 13 个只有其它工具链才用的构建文件
> （`CMakeLists.txt`、`CMakePresets.json`、`Kconfig`、`SConscript`、`component.mk`、
> `lvgl.mk`、`lvgl.pc.in`、`library.json`、`library.properties`、`idf_component.yml`、
> `.gitignore`、`.pre-commit-config.yaml`、`.typos.toml`），Keil 完全用不到这些。

删完已经验证过：**Keil 工程引用的 253 个文件一个不少，11 条头文件搜索路径全部有效，
重新编译仍然是 `0 Error(s), 0 Warning(s)`。**

想再瘦身的话，下面这些目录**不参与编译**，可以删
（删了以后想开对应功能就得重新拷源码）：

| 目录 | 大小 | 内容 | 对应开关 |
|---|---|---|---|
| `src/libs/thorvg` | 2.0 MB | 矢量图渲染 | `LV_USE_THORVG` = 0 |
| `src/libs/lodepng` | 0.4 MB | PNG 解码 | `LV_USE_LODEPNG` = 0 |
| `src/libs/freetype` | 0.4 MB | FreeType 字体引擎 | `LV_USE_FREETYPE` = 0 |
| `src/libs/` 其余 | 约 3 MB | jpeg/gif/svg/tiny_ttf… | 全部为 0 |
| `src/drivers/*`（除 `st_ltdc`） | 约 0.3 MB | 各种屏/触摸 IC 的官方驱动 | 全部为 0 |
| `src/others/*`（除 `observer`、`sysmon`） | 约 0.1 MB | XML / monkey / snapshot… | 全部为 0 |

> 绝大多数体积是 `src/font/` 里的字库（Montserrat 各字号 + 两个中文字库，合计约 10 MB）。
> 其中只有你在 `lv_conf.h` 里打开的字号会真的编进固件，留在磁盘上不影响 Flash 占用。

### 10.2 Keil 编译产物：约 241 MB（这是 Keil 自己生成的，正常现象）

`MDK-ARM/Project/` 里是 Keil 为 250 个源文件生成的中间文件：

| 文件 | 数量 | 大小 | 说明 |
|---|---|---|---|
| `.o` | 250 | **114 MB** | 目标文件，**带完整调试信息** |
| `.crf` | 249 | **110 MB** | 浏览信息，IDE 里「跳转到定义 / 符号浏览」用的数据库 |
| `.axf` | 1 | 8 MB | 最终可执行文件（带调试信息） |
| `.map` `.d` `.dep` `.htm` | 约 500 | 7 MB | 映射表、依赖关系、静态调用图 |
| `.hex` | 1 | 1 MB | 烧录用 |

**这部分不是我加进来的**，是编译 250 个源文件的必然产物（源文件多，中间文件就多）。
三种处理方式，按需选：

1. **想保留增量编译（推荐，默认就这样）**：什么都不用做。
   删掉 `.o` 后下次编译要全量重编，约 3 分钟。
2. **想省掉那 110 MB**：关掉浏览信息 ——
   Keil：`Options for Target → Output → 取消勾选 Browse Information`。
   代价只是 IDE 里「跳转到定义」变慢或失效，**完全不影响生成的固件**。
3. **想彻底清干净**：直接删掉整个 `MDK-ARM/Project/` 目录
   （下次编译自动重建，约 3 分钟）。

> ⚠️ **固件大小和这些文件夹无关**：烧进芯片的只有第 6 章那 **1.11 MB**。
> 磁盘占用大只是「源码 + 编译中间文件」多。

---

## 11. 附：一个最小调用示例

```c
/* 启动 */
sdram_init();
lcd_init();
lv_port_init();

lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Hello LVGL 9.3");
lv_obj_center(label);
lv_init();                   /* 或直接 lv_port_init() */
ui_init();                   /* 建两个界面（UI/ui.c），显示主界面 */
lv_timer_handler();          /* 画第一帧 */

gt911_init();
lcd_backlight_set(100U);     /* 复位动作做完才开背光 */

/* 主循环 */
while (1) {
    uint32_t next_ms = lv_timer_handler();
    if (next_ms > 5U) { next_ms = 5U; }
    HAL_Delay(next_ms);
}
```
