#pragma once

#include <QColor>
#include <QtGlobal>

/*
 * Win32 窗口视觉效果封装（毛玻璃 / 圆角 / 深色标题栏 / 隐藏任务栏图标）。
 * 全部走原生 API，QML 侧只负责把背景留成半透明。
 *
 * 模糊材质两条路：
 *   SetWindowCompositionAttribute + ACCENT_ENABLE_ACRYLICBLURBEHIND
 *     —— 无边框 + 逐像素透明的窗口上最稳，Win10 1803+ / Win11 都能用
 *   DwmSetWindowAttribute + DWMWA_SYSTEMBACKDROP_TYPE
 *     —— 只对「不透明客户区」的常规窗口可靠，留给 Mica 系列用
 *
 * 圆角用 SetWindowRgn 手工裁区域：DWM 自带的圆角只有 ~8px 且没法调，
 * 和 QML 里 18px 的圆角对不上，会出现角上一条硬边。
 */
namespace WindowEffects {

enum Backdrop {
    BackdropNone = 0,
    BackdropAcrylic,   // 丙烯酸（推荐，无边框窗口用这个）
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
