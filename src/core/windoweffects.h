#pragma once

#include <QColor>
#include <QtGlobal>

/*
 * Win32 窗口视觉效果封装（毛玻璃 / 圆角 / 深色标题栏 / 隐藏任务栏图标）。
 * 全部走原生 API，QML 侧只负责把背景留成半透明。
 *
 *   Win11 (build >= 22000)：DWM 系统背景材质 DWMSBT_TRANSIENTWINDOW
 *   Win10 1803+          ：SetWindowCompositionAttribute 丙烯酸
 *   其他                  ：不处理，退回纯半透明
 */
namespace WindowEffects {

enum Backdrop {
    BackdropNone = 0,
    BackdropAcrylic,   // Win10 丙烯酸
    BackdropMica,      // Win11 主材质
    BackdropMicaAlt,
    BackdropTransient, // Win11 浮出面板（最适合弹出式启动台）
};

void applyBackdrop(quintptr hwnd, Backdrop mode, const QColor &tint);
void setRoundedCorners(quintptr hwnd, bool rounded);
void setDarkMode(quintptr hwnd, bool dark);
void hideFromTaskbar(quintptr hwnd);
void setTopMost(quintptr hwnd, bool on);

bool isWindows11OrGreater();

} // namespace WindowEffects
