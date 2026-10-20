#include "LauncherWindow.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

#include "Controller.h"
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

// show() 之后多久内的 WindowDeactivate 视为「焦点还没交接到我们」而忽略
constexpr int kDeactivateGraceMs = 300;

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
    m_lastShow.start(); // 给 WindowDeactivate 一个宽限期，见 event()

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
        m_hideReason = "toggle";
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
    wlTrace("showEvent");   // 可见性时间线：和 hideEvent / --dump-icons 的 poll 打点对齐看
    if (!m_chromeReady) {
        applyWindowChrome();
        m_chromeReady = true;
    }
}

void LauncherWindow::hideEvent(QHideEvent *event)
{
    QQuickWindow::hideEvent(event);
    // 光看 hideEvent 不知道是谁让它收的 —— 原因在这里。
    wlTrace(QStringLiteral("hideEvent: reason=%1  距上次显示=%2ms")
                .arg(QString::fromLatin1(m_hideReason))
                .arg(m_lastShow.isValid() ? static_cast<int>(m_lastShow.elapsed()) : -1));
    m_hideReason = "unknown";
}

bool LauncherWindow::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            /*
             * Esc 先退一层：在文件夹里就回顶层，顶层才收起面板。
             *
             * 这个处理放在窗口级是因为 QQuickWindow::event() 比 QML 的
             * Keys.onEscapePressed 更早拿到按键 —— 只有搜索框有焦点时才轮到
             * SearchBar 自己处理；点了瓦片之后焦点在 GridView 上，那就走到这里。
             * 以前这里无条件 hidePanel()，表现为「进了文件夹按 Esc 整个面板没了」。
             */
            if (m_controller && !m_controller->openGroup().isEmpty()) {
                m_controller->closeGroup();
                return true;
            }
            m_hideReason = "esc";
            hidePanel();
            return true;
        }
    }

    if (event->type() == QEvent::WindowDeactivate && m_hideOnFocusOut && isVisible()) {
        /*
         * 刚 show() 出来的那一瞬间也会收到 WindowDeactivate：
         * 窗口还没拿到焦点，QGuiApplication::applicationState() 这时还不算
         * ApplicationActive（进程刚启动、或者焦点正在交接）。此时按「应用失活」
         * 处理的话，面板一弹出来就把自己收走 —— 实测就是
         * showEvent -> WindowDeactivate -> hideEvent，看起来像「按了热键没反应」。
         * 所以弹出后 300ms 内的失活事件直接忽略。
         */
        if (m_lastShow.isValid() && m_lastShow.elapsed() < kDeactivateGraceMs) {
            wlTrace(QStringLiteral("WindowDeactivate 忽略（show 后 %1ms）").arg(m_lastShow.elapsed()));
            return QQuickWindow::event(event);
        }

        /*
         * 焦点离开主窗口时不要立刻收起：右键菜单 / 设置窗口 / ToolTip 都是本应用
         * 自己的原生弹出窗口，弹出时会先让主窗口收到 WindowDeactivate，但
         * QGuiApplication::applicationState() 仍然是 Qt::ApplicationActive。
         * 只有应用整体失活（点桌面、Alt-Tab 到别的应用）时才隐藏。
         */
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            m_hideReason = "focus-out";
            hidePanel();
        }
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
