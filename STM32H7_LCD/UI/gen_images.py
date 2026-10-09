#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
把 image/ 下的 PNG 转成 LVGL 9 能直接用的 C 图像数组。

用法（PowerShell，用工程自带的 Python 解释器）：
    & "C:\\Users\\Zi\\.dsh\\dsh-runtimes\\dsh-primary-runtime\\dependencies\\python\\python.exe" UI\\gen_images.py

输入 / 输出：
    image/background1.png  (1920x1080 照片)  -->  UI/img_background.c/.h   800x480  RGB565
    image/light.png        (200x200 带透明)  -->  UI/img_light.c/.h        200x200  ARGB8888

换图只要把 image/ 下的同名文件替换掉，再跑一次本脚本即可。
改分辨率 / 图标尺寸就改下面的 PANEL_W / PANEL_H / ICON_SIZE。

⚠️ 生成的文件很大（背景图约 2MB 源码），这是正常的：
   800x480 的照片按 RGB565 存下来就是 768,000 字节，
   写成 C 数组每个字节约 2.8 个字符。LVGL 官方转换器也是这样输出的。
"""

import os
import numpy as np
from PIL import Image

# ------------------------- 可调参数 -------------------------
PANEL_W, PANEL_H = 800, 480     # 面板分辨率（必须和 lcd.h 的 LCD_WIDTH/HEIGHT 一致）
ICON_SIZE        = 64           # 图标显示尺寸。64 差不多是电脑桌面快捷方式图标的大小
                                # （Windows 默认"中等图标"是 48，这里取 64，触摸也好按一些）。
                                # 想改大小就改这一个数，然后重跑本脚本。
ICON_WHITE_BG    = True         # True  = 把图标的透明背景合成成白色（不透明）
                                # False = 保留透明通道（输出 ARGB8888）
WORDS_PER_LINE   = 8            # 每个 C 数组行放几个 32bit 值
# -----------------------------------------------------------

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "image")
DST_DIR = os.path.join(ROOT, "UI")


def cover_resize(im, w, h):
    """等比放大到刚好铺满 w*h，再把多出来的部分居中裁掉。

    这样图片不会变形，也不会出现黑边（原图 16:9，面板 5:3，会裁掉一点左右）。
    """
    scale = max(w / im.width, h / im.height)
    nw = max(w, int(round(im.width * scale)))
    nh = max(h, int(round(im.height * scale)))
    im = im.resize((nw, nh), Image.LANCZOS)
    left = (nw - w) // 2
    top = (nh - h) // 2
    return im.crop((left, top, left + w, top + h))


def to_rgb565_words(im):
    """转成 RGB565，并且每 2 个像素打包进 1 个 uint32。

    小端 MCU 上，uint32 的低 16 位排在前面，
    所以打包后内存里的字节序正好是连续的 uint16 RGB565 —— 就是 LVGL 要的格式。
    """
    a = np.asarray(im.convert("RGB"), dtype=np.uint16)
    v = ((a[:, :, 0] & 0xF8) << 8) | ((a[:, :, 1] & 0xFC) << 3) | (a[:, :, 2] >> 3)
    # 宽度必须是偶数才能两两配对（800 / 200 都是偶数）
    if v.shape[1] % 2:
        raise ValueError("图片宽度必须是偶数")
    lo = v[:, 0::2].astype(np.uint32)
    hi = v[:, 1::2].astype(np.uint32)
    return (lo | (hi << 16)).reshape(-1)


def to_argb8888_words(im):
    """转成 ARGB8888（保留透明通道），1 个像素 1 个 uint32：0xAARRGGBB"""
    a = np.asarray(im.convert("RGBA"), dtype=np.uint32)
    r, g, b, al = a[:, :, 0], a[:, :, 1], a[:, :, 2], a[:, :, 3]
    return ((al << 24) | (r << 16) | (g << 8) | b).reshape(-1)


def write_pair(name, words, w, h, cf, stride, data_size, desc, src_note):
    """生成 <name>.c / <name>.h"""
    guard = "__%s_H" % name.upper()

    hdr = """/**
  ******************************************************************************
  * @file    {name}.h
  * @brief   {desc}（{w}x{h} {cf}）
  *
  *  ⚠️ 本文件由 UI/gen_images.py 自动生成，**不要手改**。
  *     要换图：替换 image/ 下的 {src} 再跑一次脚本。
  ******************************************************************************
  */
#ifndef {guard}
#define {guard}

#ifdef __cplusplus
extern "C" {{
#endif

#include "lvgl.h"

/* 图像描述符，用法：lv_image_set_src(img, &{name}); */
extern const lv_image_dsc_t {name};

#ifdef __cplusplus
}}
#endif
#endif /* {guard} */
""".format(name=name, desc=desc, w=w, h=h, cf=cf, guard=guard, src=src_note)

    body_lines = []
    for i in range(0, len(words), WORDS_PER_LINE):
        chunk = words[i:i + WORDS_PER_LINE]
        body_lines.append(",".join("0x%08X" % int(v) for v in chunk) + ",")
    body = "\n".join(body_lines)

    src = """/**
  ******************************************************************************
  * @file    {name}.c
  * @brief   {desc}（{w}x{h} {cf}）
  *
  *  ⚠️ 本文件由 UI/gen_images.py 自动生成，**不要手改**（{nlines} 行数据）。
  *     要换图：替换 image/ 下的 {srcfile} 再跑一次脚本。
  ******************************************************************************
  */
#include "{name}.h"

/* 像素数据。用 uint32 打包存放，既保证 4 字节对齐，源码也比逐字节紧凑 */
static const uint32_t {name}_map[{nwords}] = {{
{body}
}};

const lv_image_dsc_t {name} = {{
    .header.magic  = LV_IMAGE_HEADER_MAGIC,
    .header.cf     = {cf},
    .header.flags  = 0,
    .header.w      = {w},
    .header.h      = {h},
    .header.stride = {stride},
    .data_size     = {data_size},
    .data          = (const uint8_t *){name}_map,
}};
""".format(name=name, desc=desc, w=w, h=h, cf=cf, srcfile=src_note,
           nlines=len(body_lines), nwords=len(words), body=body,
           stride=stride, data_size=data_size)

    with open(os.path.join(DST_DIR, name + ".h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(hdr)
    with open(os.path.join(DST_DIR, name + ".c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(src)

    csize = os.path.getsize(os.path.join(DST_DIR, name + ".c"))
    print("  %-22s %4dx%-4d %-18s data=%8d B   C源码=%6.2f MB"
          % (name, w, h, cf, data_size, csize / 1024 / 1024))


def main():
    if not os.path.isdir(DST_DIR):
        os.makedirs(DST_DIR)

    print("从 %s 生成到 %s" % (SRC_DIR, DST_DIR))

    # ---------- 背景图 ----------
    bg = cover_resize(Image.open(os.path.join(SRC_DIR, "background1.png")).convert("RGB"),
                      PANEL_W, PANEL_H)
    bg_words = to_rgb565_words(bg)
    write_pair("img_background", bg_words, PANEL_W, PANEL_H,
               "LV_COLOR_FORMAT_RGB565", PANEL_W * 2, PANEL_W * PANEL_H * 2,
               "界面背景图", "background1.png")

    # ---------- light 图标 ----------
    icon = Image.open(os.path.join(SRC_DIR, "light.png")).convert("RGBA")
    if icon.size != (ICON_SIZE, ICON_SIZE):
        icon = icon.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)

    if ICON_WHITE_BG:
        # 原图是"透明背景 + 青色图形"，这里把透明部分合成成白色。
        # 合成之后整张图完全不透明，就不需要 alpha 通道了，用 RGB565 存，
        # 字节数只有 ARGB8888 的一半（64*64*2 = 8,192 B）。
        white = Image.new("RGBA", icon.size, (255, 255, 255, 255))
        icon = Image.alpha_composite(white, icon).convert("RGB")
        icon_words = to_rgb565_words(icon)
        write_pair("img_light", icon_words, ICON_SIZE, ICON_SIZE,
                   "LV_COLOR_FORMAT_RGB565", ICON_SIZE * 2, ICON_SIZE * ICON_SIZE * 2,
                   "亮度图标（白底）", "light.png")
    else:
        icon_words = to_argb8888_words(icon)
        write_pair("img_light", icon_words, ICON_SIZE, ICON_SIZE,
                   "LV_COLOR_FORMAT_ARGB8888", ICON_SIZE * 4, ICON_SIZE * ICON_SIZE * 4,
                   "亮度图标（带透明通道）", "light.png")

    print("完成。记得把 UI/*.c 加进 Keil 工程的 UI 分组。")


if __name__ == "__main__":
    main()
