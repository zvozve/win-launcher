#include "core/windoweffects.h"

#include <QOperatingSystemVersion>

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

// QColor -> Win10 丙烯酸需要的 ABGR 整数
int colorToAbgr(const QColor &c)
{
    return static_cast<int>((c.alpha() << 24) | (c.blue() << 16) | (c.green() << 8) | c.red());
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

    if (isWindows11OrGreater() && mode != BackdropAcrylic) {
        SystemBackdropType type = kBackdropTransient;
        if (mode == BackdropMica)
            type = kBackdropMica;
        else if (mode == BackdropMicaAlt)
            type = kBackdropTabbed;
        else if (mode == BackdropNone)
            type = kBackdropNone;

        const BOOL dark = tint.lightness() < 128 ? TRUE : FALSE;
        DwmSetWindowAttribute(h, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
        DwmSetWindowAttribute(h, kDwmwaSystemBackdropType, &type, sizeof(type));
        return;
    }

    SetWindowCompositionAttributeFn setAttr = resolveSetWindowCompositionAttribute();
    if (!setAttr)
        return;

    AccentPolicy policy{};
    policy.accentState = (mode == BackdropNone) ? kAccentDisabled : kAccentEnableAcrylicBlurBehind;
    policy.accentFlags = 2;
    policy.gradientColor = colorToAbgr(tint);

    WindowCompositionAttributeData data{};
    data.attribute = kWcaAccentPolicy;
    data.data = &policy;
    data.dataSize = sizeof(policy);
    setAttr(h, &data);
}

void setRoundedCorners(quintptr hwnd, bool rounded)
{
    if (!hwnd || !isWindows11OrGreater())
        return;
    const WindowCornerPreference pref = rounded ? kCornerRound : kCornerDoNotRound;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(hwnd), kDwmwaWindowCornerPreference,
                          &pref, sizeof(pref));
}

void setDarkMode(quintptr hwnd, bool dark)
{
    if (!hwnd)
        return;
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(hwnd), kDwmwaUseImmersiveDarkMode,
                          &value, sizeof(value));
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
