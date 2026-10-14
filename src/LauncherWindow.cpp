#include "LauncherWindow.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

#include "HotkeyManager.h"
#include "DebugTrace.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

constexpr int kMinPanelWidth = 760;
constexpr int kMinPanelHeight = 480;
constexpr int kMaxPanelWidth = 1280;
constexpr int kMaxPanelHeight = 900;

} // namespace

LauncherWindow::LauncherWindow(QWindow *parent)
    : QQuickWindow(parent)
{
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setColor(Qt::transparent); // 必须透明，否则原生毛玻璃被盖住
    setTitle(QStringLiteral("WinLauncher"));
}

void LauncherWindow::setHideOnFocusOut(bool v)
{
    if (m_hideOnFocusOut == v)
        return;
    m_hideOnFocusOut = v;
    emit hideOnFocusOutChanged();
}

void LauncherWindow::setDarkBackdrop(bool v)
{
    if (m_darkBackdrop == v)
        return;
    m_darkBackdrop = v;
    emit darkBackdropChanged();
    if (m_nativeReady)
        applyNativeBackdrop();
}

void LauncherWindow::setPanelWidthRatio(qreal v)
{
    if (qFuzzyCompare(m_widthRatio, v))
        return;
    m_widthRatio = v;
    emit panelWidthRatioChanged();
}

void LauncherWindow::setPanelHeightRatio(qreal v)
{
    if (qFuzzyCompare(m_heightRatio, v))
        return;
    m_heightRatio = v;
    emit panelHeightRatioChanged();
}

void LauncherWindow::setCornerRadius(qreal v)
{
    const qreal clamped = qBound(0.0, v, 40.0);
    if (qFuzzyCompare(m_cornerRadius, clamped))
        return;
    m_cornerRadius = clamped;
    emit cornerRadiusChanged();
    if (m_nativeReady)
        applyRoundedRegion();
}

void LauncherWindow::applyRoundedRegion()
{
    const quintptr id = winId();
    if (!id)
        return;
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
    WindowEffects::setRoundedRegion(id, m_cornerRadius, dpr);
}

void LauncherWindow::applyNativeBackdrop()
{
    wlTrace("  applyNativeBackdrop enter");
    const quintptr hwnd = winId();
    if (!hwnd)
        return;

    // 材质自身的染色：alpha 越低越"透"、模糊越明显。QML 侧面板只再叠一层极淡的
    // 底色 + 描边，避免把模糊盖掉。
    const QColor tint = m_darkBackdrop ? QColor(22, 22, 26, 150) : QColor(250, 250, 252, 138);
    WindowEffects::applyBackdrop(hwnd, WindowEffects::BackdropTransient, tint);
    // 圆角交给 SetWindowRgn：DWM 自带圆角只有 ~8px，和 QML 的 18px 对不上会露硬边
    WindowEffects::setRoundedCorners(hwnd, false);
    WindowEffects::setDarkMode(hwnd, m_darkBackdrop);
    WindowEffects::hideFromTaskbar(hwnd);
    WindowEffects::setTopMost(hwnd, true);
    applyRoundedRegion();
    wlTrace("  applyNativeBackdrop done");
}

void LauncherWindow::moveToScreen(const QScreen *screen)
{
    const QScreen *target = screen;
    if (!target)
        target = QGuiApplication::screenAt(QCursor::pos());
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;

    const QRect avail = target->availableGeometry();

    const int w = qBound(kMinPanelWidth, static_cast<int>(avail.width() * m_widthRatio), kMaxPanelWidth);
    const int h = qBound(kMinPanelHeight, static_cast<int>(avail.height() * m_heightRatio), kMaxPanelHeight);

    // 略微上移，视觉上更接近 macOS 的「浮在中间偏上」
    const int x = avail.x() + (avail.width() - w) / 2;
    const int y = avail.y() + qMax(0, (avail.height() - h) / 2 - avail.height() / 12);

    resize(w, h);
    setPosition(x, y);
}

void LauncherWindow::showAtCursor()
{
    wlTrace("showAtCursor enter");
    moveToScreen(nullptr);
    show();
    raise();
    requestActivate();
    wlTrace("showAtCursor done");
}

void LauncherWindow::toggleAtCursor()
{
    if (isVisible()) {
        hidePanel();
        return;
    }
    showAtCursor();
}

void LauncherWindow::hidePanel()
{
    if (!isVisible())
        return;
    hide();
}

void LauncherWindow::showEvent(QShowEvent *event)
{
    wlTrace("showEvent enter");
    QQuickWindow::showEvent(event);

    if (!m_nativeReady) {
        applyNativeBackdrop();
        m_nativeReady = true;
    }
    moveToScreen(nullptr);
    applyRoundedRegion();
    wlTrace("showEvent done");
}

void LauncherWindow::resizeEvent(QResizeEvent *event)
{
    QQuickWindow::resizeEvent(event);
    applyRoundedRegion();
}

bool LauncherWindow::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            hidePanel();
            return true;
        }
    }

    if (event->type() == QEvent::WindowDeactivate && m_hideOnFocusOut && isVisible()) {
        // 焦点只是移交给我们自己的另一个窗口（编辑窗）时不收起
        QWindow *active = QGuiApplication::focusWindow();
        if (active != this)
            hidePanel();
    }

    return QQuickWindow::event(event);
}

bool LauncherWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    if (m_hotkeys && message) {
        auto *msg = static_cast<MSG *>(message);
        if (m_hotkeys->handleNativeMessage(reinterpret_cast<quintptr>(msg->hwnd), msg->message,
                                           static_cast<quintptr>(msg->wParam))) {
            if (result)
                *result = 0;
            return true;
        }
    }
    return QQuickWindow::nativeEvent(eventType, message, result);
}
