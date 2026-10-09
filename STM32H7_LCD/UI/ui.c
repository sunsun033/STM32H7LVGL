/**
  ******************************************************************************
  * @file    ui.c
  * @brief   LVGL 界面：背景图 + 左上角亮度入口 + 亮度调节面板（浮层）
  *
  *  ★ 渲染方案（为什么这样省）：
  *    只有一个屏幕。按下左上角的图标时**不切屏、不重画背景**，
  *    而是把一块亮度面板当浮层显示出来：
  *
  *      1. 面板做成**不透明**（bg_opa = LV_OPA_COVER）+ **直角**（radius = 0）
  *         -> LVGL 的"覆盖检查"（LV_EVENT_COVER_CHECK）会判定面板完全遮住了
  *            它下面那块区域，于是**底下的背景图整块跳过不画**
  *         ⚠️ 一旦把 radius 改成非 0，覆盖检查就会失败（四角落在圆角外），
  *            每次重绘面板都要把底下的背景重新画一遍 —— 想圆角就得接受这个代价
  *      2. 面板只占 620x250（全屏的 40%），滑入/收起时每帧只有
  *         "面板 + 它挪开后露出的那一条 620x24 的背景"需要重绘
  *
  *    对比之前"两个屏幕 + lv_screen_load_anim 滑动切换"的方案：
  *    那种做法每一动画帧都要把整屏 38 万像素的 RGB565 背景重新转成 ARGB8888，
  *    外加双缓冲同步拷贝，帧率被拖死。
  ******************************************************************************
  */
#include "ui.h"
#include "lcd.h"                /* lcd_backlight_set() */
#include "img_background.h"     /* img_background       */
#include "img_light.h"          /* img_light            */

/* 面板出现/收起动画时长（ms），以及滑入时从下方偏移多少像素 */
#define UI_PANEL_ANIM_MS    180U
#define UI_PANEL_SLIDE      24

/* 面板尺寸 */
#define UI_PANEL_W          620
#define UI_PANEL_H          250

/* 主题色 */
#define UI_COLOR_ACCENT     lv_color_hex(0x00D0FF)
#define UI_COLOR_PANEL      lv_color_hex(0x101820)
#define UI_COLOR_BTN        lv_color_hex(0x2A3644)

static lv_obj_t *s_scr;             /* 唯一的屏幕（背景 + 入口 + 面板都在上面） */
static lv_obj_t *s_panel;           /* 亮度面板（浮层） */
static lv_obj_t *s_label_bright;    /* 面板里的亮度数值 */
static lv_obj_t *s_slider;          /* 亮度滑条 0~100   */
static uint8_t   s_panel_shown;     /* 面板当前是否显示 */

/* ============================ 公共部分 ============================ */

/* 新建一个干净的空屏幕：去掉滚动、内边距、边框，
   否则背景图铺上去之后会被内边距挤偏，或者能上下拖动。 */
static lv_obj_t *create_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);    /* 参数传 NULL = 新建一个屏幕 */

    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);

    return scr;
}

/* 铺背景图。图片就是 800x480，和屏幕一样大，左上角对齐即可 */
static void add_background(lv_obj_t *scr)
{
    lv_obj_t *bg = lv_image_create(scr);

    lv_image_set_src(bg, &img_background);
    lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    /* 背景图不接收点击，免得把图标的点击事件吃掉 */
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
}

/* ============================ 面板的显示 / 收起 ============================ */

static void panel_slide_anim_cb(void *obj, int32_t v)
{
    /* 只改 translate_y，不动布局 —— 这样每一步只是"面板整体挪一点" */
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, LV_PART_MAIN);
}

static void panel_slide_to(int32_t from, int32_t to, lv_anim_completed_cb_t done_cb)
{
    lv_anim_t a;

    /* 先掐掉可能还在跑的旧动画，避免快速连点时两个动画打架 */
    (void)lv_anim_delete(s_panel, panel_slide_anim_cb);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, panel_slide_anim_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, UI_PANEL_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (done_cb != NULL) { lv_anim_set_completed_cb(&a, done_cb); }
    (void)lv_anim_start(&a);
}

/* 收起动画放完 -> 真正隐藏（隐藏后完全不参与绘制） */
static void panel_hide_done(lv_anim_t *a)
{
    LV_UNUSED(a);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_panel_shown = 0U;
}

static void panel_show(void)
{
    if (s_panel_shown != 0U) { return; }

    lv_obj_set_style_translate_y(s_panel, UI_PANEL_SLIDE, LV_PART_MAIN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_panel_shown = 1U;

    panel_slide_to(UI_PANEL_SLIDE, 0, NULL);
}

static void panel_hide(void)
{
    if (s_panel_shown == 0U) { return; }

    panel_slide_to(0, UI_PANEL_SLIDE, panel_hide_done);
}

/* ============================ 事件回调 ============================ */

/* 拖滑条 -> 实时改背光亮度 */
static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t   value  = lv_slider_get_value(slider);

    /* 滑条 0~100 和 lcd_backlight_set() 的入参含义完全一致，直接透传。
       ⚠️ 拖到 0 背光就全灭了，屏幕会黑掉（再往回拖还能调回来，因为触摸没有停）。 */
    lcd_backlight_set((uint8_t)value);
    lv_label_set_text_fmt(s_label_bright, "%d %%", (int)value);
}

/* 右上角的 × -> 收起面板 */
static void close_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    panel_hide();
}

/* 左上角的灯图标 -> 弹出面板 */
static void light_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    panel_show();
}

/* ============================ 面板 ============================ */

static void create_panel(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *close;
    lv_obj_t *close_label;

    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, UI_PANEL_W, UI_PANEL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);

    /* ★ 性能关键：不透明 + 直角，覆盖检查才会通过，底下的背景图才不用重画。
       （改成 LV_OPA_50 那种半透明、或者 radius 非 0，都会让这次优化失效） */
    lv_obj_set_style_bg_color(s_panel, UI_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_panel, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);   /* 上电先藏着 */

    title = lv_label_create(s_panel);
    lv_label_set_text(title, "Brightness");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    s_label_bright = lv_label_create(s_panel);
    lv_label_set_text(s_label_bright, "100 %");
    lv_obj_set_style_text_font(s_label_bright, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_bright, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_align(s_label_bright, LV_ALIGN_CENTER, 0, -8);

    s_slider = lv_slider_create(s_panel);
    lv_slider_set_range(s_slider, 0, 100);              /* 0 ~ 100 */
    lv_obj_set_width(s_slider, 520);
    lv_slider_set_value(s_slider, 100, LV_ANIM_OFF);    /* 初始 100 = 最亮 */
    lv_obj_set_style_bg_color(s_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(s_slider, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(s_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 右上角的 × 号：圆形小按钮，用 LVGL 内置的符号字体画叉
       （内置的 Montserrat 字体已经包含 0xF000~0xF8A3 这段符号区，不用额外加字体） */
    close = lv_button_create(s_panel);
    lv_obj_set_size(close, 40, 40);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close, UI_COLOR_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close, 0, LV_PART_MAIN);   /* 去掉主题默认的阴影 */
    lv_obj_add_event_cb(close, close_event_cb, LV_EVENT_CLICKED, NULL);

    close_label = lv_label_create(close);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(close_label);
}

/* ============================ 左上角的入口 ============================ */

static void create_entry(lv_obj_t *parent)
{
    lv_obj_t *icon;
    lv_obj_t *label;

    /* 图标本身是**白底**的（在 UI/gen_images.py 里把透明背景合成成了白色），
       尺寸 64x64，距屏幕左上角 16px。
       按下时整体变暗一点当反馈（LV_STATE_PRESSED 状态下的 opa 样式）。 */
    icon = lv_image_create(parent);
    lv_image_set_src(icon, &img_light);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(icon, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(icon, light_event_cb, LV_EVENT_CLICKED, NULL);

    /* 图标下面跟一行小名字，就像桌面图标底下的文字。
       背景是照片，所以给文字垫一层白底 + 深色字，保证看得清。 */
    label = lv_label_create(parent);
    lv_label_set_text(label, "Brightness");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN);
    lv_obj_align_to(label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

/* ============================ 对外接口 ============================ */

void ui_init(void)
{
    s_scr = create_screen();

    /* 添加顺序 = 绘制顺序：背景在最下面，面板最后加也就是在最上面 */
    add_background(s_scr);
    create_entry(s_scr);
    create_panel(s_scr);

    s_panel_shown = 0U;

    /* 上电显示（只有一个屏幕，不需要切换）。
       注意这里**不设背光亮度**：背光由 main() 在 gt911_init() 之后统一打开，
       否则面板复位那一下会当着用户的面闪一下。 */
    lv_screen_load(s_scr);
}
