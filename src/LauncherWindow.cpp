#include "LauncherWindow.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

#include "DebugTrace.h"
#include "HotkeyManager.h"

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
    setColor(Qt::transparent); // 必须透明：玻璃 / 圆角 / 阴影都在 QML 里画
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
    if (m_chromeReady)
        applyWindowChrome();
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
    const qreal clamped = qBound(0.0, v, 48.0);
    if (qFuzzyCompare(m_cornerRadius, clamped))
        return;
    m_cornerRadius = clamped;
    emit cornerRadiusChanged();
}

void LauncherWindow::applyWindowChrome()
{
    const quintptr hwnd = winId();
    if (!hwnd)
        return;

    // 不再应用任何系统材质：玻璃底图是 QML 里的一张模糊截图。
    // 这里只处理「虚拟窗口属性」——不进任务栏、始终置顶、深色标题栏。
    WindowEffects::hideFromTaskbar(hwnd);
    WindowEffects::setTopMost(hwnd, true);
    WindowEffects::setDarkMode(hwnd, m_darkBackdrop);
    // DWM 自带圆角要关掉：窗口四周要留出画阴影的空间，形状由 QML 决定
    WindowEffects::setRoundedCorners(hwnd, false);
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

    const int panelW =
        qBound(kMinPanelWidth, static_cast<int>(avail.width() * m_widthRatio), kMaxPanelWidth);
    const int panelH =
        qBound(kMinPanelHeight, static_cast<int>(avail.height() * m_heightRatio), kMaxPanelHeight);

    // 窗口 = 面板 + 四周阴影边距；居中时按窗口算，面板自然也是居中的
    const int w = panelW + 2 * kShadowMargin;
    const int h = panelH + 2 * kShadowMargin;

    // 略微上移，视觉上更接近 macOS 的「浮在中间偏上」
    const int x = avail.x() + (avail.width() - w) / 2;
    const int y = avail.y() + qMax(0, (avail.height() - h) / 2 - avail.height() / 12);

    resize(w, h);
    setPosition(x, y);

    m_panelRect = QRect(x + kShadowMargin, y + kShadowMargin, panelW, panelH);
}

void LauncherWindow::showAtCursor()
{
    // 顺序很重要：先摆好位置（此刻窗口还是隐藏的）→ 抓屏 → 才 show()。
    // 抓屏必须发生在窗口出现在屏幕上之前，否则会把自己的上一帧抓进底图。
    moveToScreen(nullptr);

    // 抓屏区域用 Win32 的物理矩形，而不是把 Qt 的逻辑坐标乘 dpr：
    // 多屏不同 DPR 时两者不是简单的比例关系（每个屏的物理原点 Qt 没有暴露），
    // 在副屏上会算出一个越界的区域，抓屏直接失败。
    // 未 show() 时调 winId() 只是先把原生窗口建出来，不会显示，是安全的。
    const quintptr hwnd = winId();
    RECT rc{};
    if (hwnd && GetWindowRect(reinterpret_cast<HWND>(hwnd), &rc) && rc.right > rc.left
        && rc.bottom > rc.top) {
        const QRect physical(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
        const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
        emit backdropRequested(physical, m_cornerRadius, dpr);
    } else {
        wlTrace("showAtCursor: GetWindowRect 失败，跳过底图（走纯色兜底）");
    }

    show();
    raise();
    requestActivate();

    wlTrace(QStringLiteral("showAtCursor: panel=(%1,%2 %3x%4) window=%5x%6 r=%7")
                .arg(m_panelRect.x())
                .arg(m_panelRect.y())
                .arg(m_panelRect.width())
                .arg(m_panelRect.height())
                .arg(width())
                .arg(height())
                .arg(m_cornerRadius));
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
    QQuickWindow::showEvent(event);
    if (!m_chromeReady) {
        applyWindowChrome();
        m_chromeReady = true;
    }
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
        /*
         * 焦点离开主窗口时不要立刻收起：右键菜单 / 设置窗口 / ToolTip 都是本应用
         * 自己的原生弹出窗口，弹出时会先让主窗口收到 WindowDeactivate，但
         * QGuiApplication::applicationState() 仍然是 Qt::ApplicationActive。
         * 只有应用整体失活（点桌面、Alt-Tab 到别的应用）时才隐藏。
         */
        if (QGuiApplication::applicationState() != Qt::ApplicationActive)
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
