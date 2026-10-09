/**
  ******************************************************************************
  * @file    ui.h
  * @brief   LVGL 界面（背景图 + 灯图标 -> 亮度滑条界面）
  *
  *  界面结构：
  *    界面1（主界面）  : 背景图 + light 图标 + 提示文字
  *                       按下图标 -> 左滑动画切到界面2
  *    界面2（亮度界面）: 同一张背景图 + 半透明面板 + 0~100 滑条 + 返回按钮
  *                       拖滑条实时改背光亮度；按返回 -> 右滑动画切回界面1
  *
  *  图片资源由 UI/gen_images.py 从 image/ 下的 PNG 生成，
  *  见 UI/img_background.c / UI/img_light.c。
  ******************************************************************************
  */
#ifndef __UI_H
#define __UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
  * @brief  创建所有界面并显示主界面
  * @note   必须在 lv_port_init() 之后调用（LVGL 已经初始化好）。
  *         调用时背光应该还是灭的，背光由 main() 在 gt911_init() 之后统一打开。
  */
void ui_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __UI_H */
