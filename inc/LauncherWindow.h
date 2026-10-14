#pragma once

#include <QColor>
#include <QQuickWindow>

#include "WindowEffects.h"

class HotkeyManager;

/*
 * 启动台弹出面板窗口（QML 里的根对象）。
 *
 * 与普通 Window 的区别：
 *   - 无边框 + 半透明 + 系统毛玻璃（Win11 用 DWMSBT_TRANSIENTWINDOW）
 *   - 圆角、不进任务栏、始终置顶
 *   - 出现在「鼠标所在屏幕」中央，尺寸按屏幕比例算
 *   - Esc 或失去焦点时自动收起
 *   - 承接全局热键的 WM_HOTKEY 消息
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

public:
    explicit LauncherWindow(QWindow *parent = nullptr);

    bool hideOnFocusOut() const { return m_hideOnFocusOut; }
    void setHideOnFocusOut(bool v);

    bool darkBackdrop() const { return m_darkBackdrop; }
    void setDarkBackdrop(bool v);

    qreal panelWidthRatio() const { return m_widthRatio; }
    void setPanelWidthRatio(qreal v);

    qreal panelHeightRatio() const { return m_heightRatio; }
    qreal cornerRadius() const { return m_cornerRadius; }
    void setPanelHeightRatio(qreal v);
    void setCornerRadius(qreal v);

    void setHotkeyManager(HotkeyManager *manager) { m_hotkeys = manager; }

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

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void applyNativeBackdrop();
    void applyRoundedRegion();
    void moveToScreen(const QScreen *screen);

    HotkeyManager *m_hotkeys = nullptr;
    bool m_hideOnFocusOut = true;
    bool m_darkBackdrop = true;
    qreal m_widthRatio = 0.62;
    qreal m_heightRatio = 0.66;
    // 圆角半径（逻辑像素）。DWM 自带圆角只有 ~8px 且不可调，和 QML 对不上，
    // 所以用 SetWindowRgn 按这个值裁窗口区域，让材质和 QML 面板共用同一个形状。
    qreal m_cornerRadius = 18.0;
    bool m_nativeReady = false;
};
