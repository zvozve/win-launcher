#include "WindowEffects.h"

#include <QDebug>
#include <QOperatingSystemVersion>

#include "DebugTrace.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

namespace {

// ---- 未公开接口 / 未公开常量 -------------------------------------------------
// 全部用 kXxx 私有前缀：新版 Windows SDK 的 dwmapi.h 已经导出了 DWM_SYSTEMBACKDROP_TYPE
// 等同名枚举，直接沿用官方名字会撞名。

// WCA_ACCENT_POLICY（Win10 1803+）
constexpr int kWcaAccentPolicy = 19;

enum AccentState {
    kAccentDisabled = 0,
    kAccentEnableGradient = 1,
    kAccentEnableTransparentGradient = 2,
    kAccentEnableBlurBehind = 3,
    kAccentEnableAcrylicBlurBehind = 4, // Win10 1803+
    kAccentEnableHostBackdrop = 5,
};

struct AccentPolicy
{
    int accentState;
    int accentFlags;
    int gradientColor; // ABGR
    int animationId;
};

struct WindowCompositionAttributeData
{
    int attribute;
    void *data;
    ULONG dataSize;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WindowCompositionAttributeData *);

// Win11 DWM 属性
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwaSystemBackdropType = 38;

enum SystemBackdropType {
    kBackdropAuto = 0,
    kBackdropNone = 1,
    kBackdropMica = 2,      // Mica
    kBackdropTransient = 3, // Acrylic
    kBackdropTabbed = 4,    // Mica Alt
};

enum WindowCornerPreference {
    kCornerDefault = 0,
    kCornerDoNotRound = 1,
    kCornerRound = 2,
    kCornerRoundSmall = 3,
};

SetWindowCompositionAttributeFn resolveSetWindowCompositionAttribute()
{
    static SetWindowCompositionAttributeFn fn = []() -> SetWindowCompositionAttributeFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32)
            return nullptr;
        return reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    }();
    return fn;
}

// QColor -> 丙烯酸需要的 ABGR 整数
int colorToAbgr(const QColor &c)
{
    return static_cast<int>((c.alpha() << 24) | (c.blue() << 16) | (c.green() << 8) | c.red());
}

// 把整块客户区都算进"DWM 玻璃"，否则无边框窗口上模糊只覆盖一部分
void extendFrameIntoClientArea(HWND h)
{
    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(h, &margins);
}

} // namespace

namespace WindowEffects {

bool isWindows11OrGreater()
{
    return QOperatingSystemVersion::current().majorVersion() >= 11;
}

void applyBackdrop(quintptr hwnd, Backdrop mode, const QColor &tint)
{
    if (!hwnd)
        return;
    HWND h = reinterpret_cast<HWND>(hwnd);

    if (mode == BackdropMica || mode == BackdropMicaAlt) {
        // DWM 系统材质只对「不透明客户区」的常规窗口可靠，Mica 系列才走这条
        const SystemBackdropType type =
            (mode == BackdropMica) ? kBackdropMica : kBackdropTabbed;
        const BOOL dark = tint.lightness() < 128 ? TRUE : FALSE;
        DwmSetWindowAttribute(h, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
        const HRESULT hr = DwmSetWindowAttribute(h, kDwmwaSystemBackdropType, &type, sizeof(type));
        wlTrace(QStringLiteral("  backdrop: DWMSBT hr=0x%1")
                    .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        return;
    }

    // 无边框 + 逐像素透明的窗口：丙烯酸走 SetWindowCompositionAttribute 最稳
    SetWindowCompositionAttributeFn setAttr = resolveSetWindowCompositionAttribute();
    if (!setAttr) {
        wlTrace("  backdrop: SetWindowCompositionAttribute 不可用，退回纯半透明");
        return;
    }

    extendFrameIntoClientArea(h);

    AccentPolicy policy{};
    policy.accentState =
        (mode == BackdropNone) ? kAccentDisabled : kAccentEnableAcrylicBlurBehind;
    policy.accentFlags = 2;
    policy.gradientColor = colorToAbgr(tint);

    WindowCompositionAttributeData data{};
    data.attribute = kWcaAccentPolicy;
    data.data = &policy;
    data.dataSize = sizeof(policy);

    const BOOL ok = setAttr(h, &data);
    wlTrace(QStringLiteral("  backdrop: acrylic state=%1 abgr=%2 ok=%3")
                .arg(policy.accentState)
                .arg(static_cast<quint32>(colorToAbgr(tint)), 8, 16, QLatin1Char('0'))
                .arg(ok ? 1 : 0));
}

void setRoundedCorners(quintptr hwnd, bool rounded)
{
    if (!hwnd || !isWindows11OrGreater())
        return;
    const WindowCornerPreference pref = rounded ? kCornerRound : kCornerDoNotRound;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(hwnd), kDwmwaWindowCornerPreference, &pref,
                          sizeof(pref));
}

void setRoundedRegion(quintptr hwnd, qreal radiusPx, qreal dpr)
{
    if (!hwnd)
        return;
    HWND h = reinterpret_cast<HWND>(hwnd);

    RECT rc{};
    if (!GetWindowRect(h, &rc))
        return;
    const int w = rc.right - rc.left;
    const int hgt = rc.bottom - rc.top;
    if (w <= 0 || hgt <= 0)
        return;

    const qreal scale = dpr > 0.0 ? dpr : 1.0;
    const int diameter = qMax(2, qRound(radiusPx * 2.0 * scale));
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, hgt + 1, diameter, diameter);
    if (!region)
        return;

    // 成功后 region 归系统所有，不能再 DeleteObject
    if (!SetWindowRgn(h, region, TRUE)) {
        DeleteObject(region);
        wlTrace("  region: SetWindowRgn FAILED");
        return;
    }
    wlTrace(QStringLiteral("  region: %1x%2 r=%3px dpr=%4")
                .arg(w)
                .arg(hgt)
                .arg(radiusPx)
                .arg(scale));
}

void setDarkMode(quintptr hwnd, bool dark)
{
    if (!hwnd)
        return;
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(hwnd), kDwmwaUseImmersiveDarkMode, &value,
                          sizeof(value));
}

void hideFromTaskbar(quintptr hwnd)
{
    if (!hwnd)
        return;
    HWND h = reinterpret_cast<HWND>(hwnd);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;
    ex &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
}

void setTopMost(quintptr hwnd, bool on)
{
    if (!hwnd)
        return;
    HWND h = reinterpret_cast<HWND>(hwnd);
    SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace WindowEffects
