#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QRect>
#include <QQuickWindow>

#include "WindowEffects.h"

class HotkeyManager;
class Controller;

/*
 * 启动台弹出面板窗口（QML 里的根对象）。
 *
 * 与普通 Window 的区别：
 *   - 无边框 + 全透明；玻璃、圆角、阴影全部由 QML 画，窗口本身不参与视觉
 *   - 窗口比可见面板大一圈（kShadowMargin），多出来的空间留给 QML 画外阴影
 *   - 弹出前先把面板位置的桌面像素抓下来做模糊，交给 QML 当玻璃底图
 *   - 出现在「鼠标所在屏幕」中央，尺寸按屏幕比例算
 *   - Esc 或失去焦点时自动收起
 *   - 承接全局热键的 WM_HOTKEY 消息
 *
 * 注意：这里**不**用 DWM 的系统材质（DWMWA_SYSTEMBACKDROP_TYPE /
 * SetWindowCompositionAttribute），原因见 BackdropBlur.h 的说明；也不再
 * SetWindowRgn 裁窗口区域 —— 那会把 QML 画的外阴影一起裁掉。
 */
class LauncherWindow : public QQuickWindow
{
    Q_OBJECT

    Q_PROPERTY(bool hideOnFocusOut READ hideOnFocusOut WRITE setHideOnFocusOut
                   NOTIFY hideOnFocusOutChanged)
    Q_PROPERTY(bool darkBackdrop READ darkBackdrop WRITE setDarkBackdrop NOTIFY darkBackdropChanged)
    Q_PROPERTY(qreal panelWidthRatio READ panelWidthRatio WRITE setPanelWidthRatio
                   NOTIFY panelWidthRatioChanged)
    Q_PROPERTY(qreal panelHeightRatio READ panelHeightRatio WRITE setPanelHeightRatio
                   NOTIFY panelHeightRatioChanged)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius
                   NOTIFY cornerRadiusChanged)
    Q_PROPERTY(int shadowMargin READ shadowMargin CONSTANT)

public:
    /*
     * 面板四周留给外阴影的空白（逻辑像素）。窗口尺寸 = 面板尺寸 + 2 × 这个值。
     *
     * ★ 必须 ≥ 3×shadowBlur + 阴影偏移，否则阴影还没衰减到 0 就被窗口边界切平，
     *   会在圆角面板外面露出一条直角硬边（这就是「圆角后面跟着直角背景」的原因）。
     *   当前：3×20 + 8 = 68 ≤ 72。改 PanelOptions::shadowBlurPx 时记得一起来。
     */
    static constexpr int kShadowMargin = 72;

    explicit LauncherWindow(QWindow *parent = nullptr);

    bool hideOnFocusOut() const { return m_hideOnFocusOut; }
    void setHideOnFocusOut(bool v);

    bool darkBackdrop() const { return m_darkBackdrop; }
    void setDarkBackdrop(bool v);

    qreal panelWidthRatio() const { return m_widthRatio; }
    void setPanelWidthRatio(qreal v);

    qreal panelHeightRatio() const { return m_heightRatio; }
    void setPanelHeightRatio(qreal v);

    qreal cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(qreal v);

    int shadowMargin() const { return kShadowMargin; }

    void setHotkeyManager(HotkeyManager *manager) { m_hotkeys = manager; }

    /*
     * 挂上 Controller：只为了让 Esc 能「先退一层文件夹，再收起面板」。
     * 不挂也不影响启动，只是 Esc 永远直接收起。
     * （由 Controller::attachWindow 调用，不需要 QML 参与）
     */
    void setController(Controller *c) { m_controller = c; }

    // QML 调用
    Q_INVOKABLE void showAtCursor();  // 在鼠标所在屏幕居中弹出
    Q_INVOKABLE void toggleAtCursor(); // 已显示则收起，否则弹出
    Q_INVOKABLE void hidePanel();

signals:
    void hideOnFocusOutChanged();
    void darkBackdropChanged();
    void panelWidthRatioChanged();
    void panelHeightRatioChanged();
    void cornerRadiusChanged();

    /*
     * 请 Controller 抓取窗口区域的桌面像素做玻璃底图。
     *
     * windowRectPx        窗口的**物理像素**矩形（Win32 坐标，由 GetWindowRect 取得）
     * cornerRadiusLogical 面板圆角（逻辑像素），抓图时按 dpr 换算
     * dpr                 窗口所在屏的缩放比
     *
     * 由 showAtCursor() 在 show() **之前**发出，所以必须同步处理完 —— 连接方要是
     * 异步去抓，抓到的就是自己上一帧。
     */
    void backdropRequested(const QRect &windowRectPx, qreal cornerRadiusLogical, qreal dpr);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void applyWindowChrome();
    void moveToScreen(const QScreen *screen);

    HotkeyManager *m_hotkeys = nullptr;
    Controller *m_controller = nullptr;
    bool m_hideOnFocusOut = true;
    // show() 的时刻：紧跟着到达的 WindowDeactivate 属于「焦点还没交接好」，要忽略
    QElapsedTimer m_lastShow;
    bool m_darkBackdrop = true;
    qreal m_widthRatio = 0.62;
    qreal m_heightRatio = 0.66;
    // 面板圆角（逻辑像素）。QML 用它画面板，抓屏时用它裁背景图，必须一致。
    qreal m_cornerRadius = 26.0;
    // 可见面板在屏幕上的矩形（不含阴影边距），逻辑坐标
    QRect m_panelRect;
    bool m_chromeReady = false;
};
