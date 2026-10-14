#pragma once

#include <QColor>
#include <QtGlobal>

/*
 * Win32 窗口「外观属性」封装：不进任务栏 / 始终置顶 / 深色标题栏 / 圆角开关。
 *
 * ★ 面板的模糊、圆角、外阴影**不走这里**。
 *   系统材质（DWMWA_SYSTEMBACKDROP_TYPE / SetWindowCompositionAttribute 的
 *   ACCENT_ENABLE_ACRYLICBLURBEHIND）由 DWM 按「矩形」合成，SetWindowRgn 裁不到它，
 *   于是四个角上会露出方方正正的玻璃 —— 这正是「圆角不生效」的根源；
 *   而且无边框窗口拿不到 DWM 的原生外阴影。
 *   现在改成自绘：C++ 抓屏 → 模糊 + 提饱和 → 裁圆角 → 烘外阴影 → QML 铺图。
 *   完整理由与实现见 inc/BackdropBlur.h、src/BackdropBlur.cpp。
 *
 * setRoundedCorners / setRoundedRegion 是备用手段（例如将来要做不依赖位图的
 * 轻量版本），当前没有被调用。
 */
namespace WindowEffects {

enum Backdrop {
    BackdropNone = 0,
    BackdropAcrylic,   // 丙烯酸（无边框窗口上最稳，但见上面的说明）
    BackdropMica,      // Win11 主材质
    BackdropMicaAlt,
    BackdropTransient, // 同 Acrylic，语义上表示"浮出面板"
};

void applyBackdrop(quintptr hwnd, Backdrop mode, const QColor &tint);

// DWM 自带圆角开关（用区域裁剪时传 false）
void setRoundedCorners(quintptr hwnd, bool rounded);

// 按 radiusPx（逻辑像素）裁一个圆角窗口区域；dpr 用于换算物理像素
void setRoundedRegion(quintptr hwnd, qreal radiusPx, qreal dpr);

void setDarkMode(quintptr hwnd, bool dark);
void hideFromTaskbar(quintptr hwnd);
void setTopMost(quintptr hwnd, bool on);

bool isWindows11OrGreater();

} // namespace WindowEffects
