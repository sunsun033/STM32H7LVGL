/**
  ******************************************************************************
  * @file    lv_conf.h
  * @brief   LVGL 9.3.0 配置文件（STM32H743IIT6 + 7寸 800x480 RGB666 面板）
  *
  *  说明：
  *    本文件刻意只写"需要改的项"，其余选项全部由 LVGL 自带的
  *    src/lv_conf_internal.h 提供默认值。这样配置一目了然，LVGL 升级时
  *    也不会因为模板变化而报错。
  *
  *  位置要求：
  *    lv_conf.h 必须放在 lvgl 文件夹的**同级目录**（即本文件的路径
  *    Component/LVGL/lv_conf.h 对应 Component/LVGL/lvgl/）。
  *    lv_conf_internal.h 默认按 "../../lv_conf.h" 查找。
  *
  *  与硬件的对应关系：
  *    LV_COLOR_DEPTH 16  <->  Hardware/lcd_format.h 的 LCD_LV_COLOR_DEPTH
  *                            <->  LTDC 层格式 RGB565（每像素 2 字节）
  *                            <->  DMA2D Init.ColorMode = DMA2D_OUTPUT_RGB565
  *    显存缓冲区由 Component/LVGL/port/lv_port_disp.c 指定到 SDRAM
  *
  *  ⚠️ 改像素格式请看 Hardware/lcd_format.h（那里是唯一定义处）。
  *     两边不一致时会由 lv_port_disp.c 里的 #error 直接编译报错，不会拖到花屏才发现。
  ******************************************************************************
  */

/* 防止 lv_conf_internal.h 弹出 "Possible failure to include lv_conf.h" 提示 */
#define LV_CONF_H

/*====================
   颜色设置
 *====================*/

/* 颜色深度：1/8/16/24/32。
   必须为 16（= LV_COLOR_FORMAT_RGB565）—— 与 LTDC Layer0 的 PixelFormat
   （LTDC_PIXEL_FORMAT_RGB565，2 字节/像素）严格对应，改一边就得改另一边，
   否则画面会串色 / 花屏。

   为什么用 16 而不是 32：
     1) UI/ 里的背景图和图标本来就是 RGB565，显存也是 565 时
        lv_image 的绘制退化成"整块 memcpy"，不再逐像素做 565->8888 转换；
     2) 显存减半（单缓冲 1.5MB -> 750KB），LTDC 扫描带宽也减半。
   代价：软件混合按 16 位精度做，半透明渐变可能有轻微色带（本工程界面基本不透明）。 */
#define LV_COLOR_DEPTH 16

/*====================
   标准库封装
 *====================*/

/* 内存、字符串、格式化全部用 LVGL 自带实现：
   裸机 + ARMCC 下最省心，且不依赖 newlib 的 heap（默认 heap 很小）。 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/* LVGL 自己的堆大小。控件 + 样式 + 定时器都在这里分配，
   800x480 的界面用 64KB 很宽裕（不够会打印 malloc 失败断言）。 */
#define LV_MEM_SIZE (64U * 1024U)
#define LV_MEM_ADR  0U          /* 0 = 用内部静态数组（放在 .bss） */

/*====================
   运行环境
 *====================*/

/* 裸机，无 RTOS */
#define LV_USE_OS   LV_OS_NONE

/* 渲染间隔（ms）。这就是帧率上限：1000 / LV_DEF_REFR_PERIOD。
 *
 * LVGL 9 的刷新是**按需触发**的：有东西需要重绘（invalidate）时把刷新定时器唤醒，
 * 定时器到点渲染一帧，然后自己 pause 掉；下次被 invalidate 再唤醒。
 * 而 LVGL 源码里**没有任何地方**会修改这个定时器的周期
 * （整个工程只有 ffmpeg / monkey 用过 lv_timer_set_period，都没编进来），
 * 所以它是固定的 —— 这一项直接决定"从画面变化到真正画出来"要等多久。
 *
 * 官方默认 33ms -> 帧率上限只有 30fps，对 60Hz 的 LTDC 来说白白浪费一半。
 * 这里改成 16ms -> 上限约 62fps，正好对上 800x480@60Hz 的屏。
 * 再往下调没意义（屏本身只有 60Hz），只会白耗 CPU。 */
#define LV_DEF_REFR_PERIOD 16

/* DPI，只影响默认字号/控件尺寸的观感，130 是官方默认 */
#define LV_DPI_DEF 130

/*====================
   绘图后端
 *====================*/

/* 软件渲染（必须开） */
#define LV_USE_DRAW_SW 1

/* GPU / 其它加速后端全部关闭。
   SDRAM 为非缓存，CPU 直接渲染；需要提速时再打开 DMA2D 绘制单元。 */
#define LV_USE_DRAW_DMA2D       0
#define LV_USE_DRAW_VG_LITE     0
#define LV_USE_DRAW_OPENGLES    0
#define LV_USE_NEMA_GFX         0
#define LV_USE_PXP              0
#define LV_USE_DRAW_G2D         0
#define LV_USE_DRAW_DAVE2D      0
#define LV_USE_DRAW_SDL         0

/*====================
   日志与断言
 *====================*/

/* 日志开到 WARN：出错时能通过串口看到原因。
   串口回调在 port/lv_port_log.c 里注册（USART1，115200）。 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL           LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF          0    /* 0 = 用 lv_log_register_print_cb 自定义输出 */
#define LV_LOG_USE_TIMESTAMP   0    /* 时间戳要读 tick，这里不需要 */

/* 断言只留"极快"的两个，重断言（内存完整性/对象校验）会显著拖慢 */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* 调试用的重绘/图层/并行绘制可视化，全部关闭 */
#define LV_USE_REFR_DEBUG           0
#define LV_USE_LAYER_DEBUG          0
#define LV_USE_PARALLEL_DRAW_DEBUG  0

/*====================
   字体
 *====================*/

/* 只打开用得到的字号，能省不少 Flash。
   注意：这些是 ASCII 字体，**不含中文**。
   要显示中文需要用 LVGL 的字体转换工具生成 CJK 字体后在这里注册。 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_PLACEHOLDER 1

/*====================
   主题与布局
 *====================*/

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0                 /* 0 = 浅色主题 */
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO   0

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*====================
   控件（常用集合，全部打开）
 *====================*/

#define LV_USE_ANIMIMG      1
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR     1
#define LV_USE_CANVAS       1
#define LV_USE_CHART        1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  1
#define LV_USE_KEYBOARD     1
#define LV_USE_LABEL        1
#define LV_USE_LED          1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       1
#define LV_USE_SCALE        1
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      1
#define LV_USE_TEXTAREA     1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/*====================
   ★ 屏幕驱动：LVGL 官方 ST LTDC 驱动
 *====================*/

/* 打开 LVGL 自带的 lv_st_ltdc.c：
   它以"直接渲染模式 + 双缓冲"接管 LTDC Layer，
   切页时调用 HAL_LTDC_Reload(VERTICAL_BLANKING)，由硬件在垂直消隐期
   自动换帧缓冲地址 —— 不撕裂，且不需要我们写轮询代码。

   ⚠️ 依赖 Core/Inc/stm32h7xx_hal_conf.h 里的
      USE_HAL_LTDC_REGISTER_CALLBACKS 必须为 1U（重载中断回调要用）。 */
#define LV_USE_ST_LTDC 1
#define LV_ST_LTDC_USE_DMA2D_FLUSH 0    /* 只对 PARTIAL 模式有效，本工程用 DIRECT */

/*====================
   其它可选模块（全部关闭，保持精简）
 *====================*/

/* 观察者：核心/控件内部依赖，必须为 1 */
#define LV_USE_OBSERVER 1

#define LV_USE_SYSMON        1    /* 性能/内存监视器：FPS 统计依赖它 */
#define LV_USE_PERF_MONITOR  1    /* 屏幕上实时显示 FPS + CPU 占用 */
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_TOP_LEFT   /* 显示在左上角（默认右下角） */
#define LV_USE_MEM_MONITOR   0
#define LV_USE_PROFILER      0
#define LV_USE_SNAPSHOT      0
#define LV_USE_MONKEY        0
#define LV_USE_GRIDNAV       0
#define LV_USE_FRAGMENT      0
#define LV_USE_IMGFONT       0
#define LV_USE_IME_PINYIN    0
#define LV_USE_FILE_EXPLORER 0
#define LV_USE_FONT_MANAGER  0
#define LV_USE_TEST          0
#define LV_USE_XML           0

/* 对象 ID / 名称 / 属性：调试用，不需要 */
#define LV_USE_OBJ_ID       0
#define LV_USE_OBJ_NAME     0
#define LV_USE_OBJ_PROPERTY 0

/* 双向文字 / 阿拉伯语：不需要 */
#define LV_USE_BIDI                 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* 浮点与矩阵运算：不需要 */
#define LV_USE_FLOAT  0
#define LV_USE_MATRIX 0

/*====================
   第三方库（全部关闭，源码保留在 src/libs 下，需要时再开）
 *====================*/

#define LV_USE_LODEPNG        0
#define LV_USE_LIBPNG         0
#define LV_USE_BMP            0
#define LV_USE_TJPGD          0
#define LV_USE_LIBJPEG_TURBO  0
#define LV_USE_GIF            0
#define LV_USE_RLE            0
#define LV_USE_QRCODE         0
#define LV_USE_BARCODE        0
#define LV_USE_FREETYPE       0
#define LV_USE_TINY_TTF       0
#define LV_USE_RLOTTIE        0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG_EXTERNAL 0
#define LV_USE_SVG            0
#define LV_USE_FFMPEG         0

/*====================
   官方自带驱动（不用，本工程有 Hardware/ 下的自研驱动）
 *====================*/

#define LV_USE_ST7735       0
#define LV_USE_ST7789       0
#define LV_USE_ST7796       0
#define LV_USE_ILI9341      0
#define LV_USE_FT81X        0
#define LV_USE_EVDEV        0
#define LV_USE_LIBINPUT     0
#define LV_USE_TFT_ESPI     0
#define LV_USE_SDL          0
#define LV_USE_X11          0
#define LV_USE_WAYLAND      0
#define LV_USE_LINUX_FBDEV  0
#define LV_USE_LINUX_DRM    0
#define LV_USE_WINDOWS      0
#define LV_USE_UEFI         0
#define LV_USE_OPENGLES     0
#define LV_USE_QNX          0

/*====================
   Demos / Examples（不参与编译）
 *====================*/

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS   0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS    0
#define LV_USE_DEMO_MUSIC     0
#define LV_USE_DEMO_MULTILANG 0
#define LV_USE_DEMO_FLEX_LAYOUT 0
#define LV_USE_DEMO_TRANSFORM 0
#define LV_USE_DEMO_SCROLL    0
#define LV_USE_DEMO_RENDER    0
#define LV_USE_DEMO_VECTOR_GRAPHIC 0
#define LV_USE_DEMO_EBIKE     0
#define LV_USE_DEMO_HIGH_RES  0
#define LV_USE_DEMO_SMARTWATCH 0
