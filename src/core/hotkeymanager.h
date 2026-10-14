#pragma once

#include <QObject>
#include <QString>

/*
 * 全局热键（可配置）。
 *
 * 用 RegisterHotKey 注册到启动台窗口的 HWND，WM_HOTKEY 从
 * LauncherWindow::nativeEvent 转进来触发面板开合。
 * 序列文本形如 "Ctrl+Alt+Space"，由设置界面录制后写进 Config。
 */
class HotkeyManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString sequence READ sequence NOTIFY sequenceChanged)
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // 必须在窗口首次 show 之后调用（此时才有有效 HWND）
    void attachWindow(quintptr hwnd);

    QString sequence() const { return m_sequence; }
    bool isRegistered() const { return m_registered; }

    // 设置并立即重新注册；失败时保持原热键并返回 false
    bool setSequence(const QString &sequence, QString *error = nullptr);

    // 由 LauncherWindow::nativeEvent 转发
    bool handleNativeMessage(quintptr hwnd, unsigned int message, quintptr wParam);

    static QString defaultSequence() { return QStringLiteral("Ctrl+Alt+Space"); }

signals:
    void activated();
    void sequenceChanged();
    void registeredChanged();

private:
    bool apply();
    void release();

    QString m_sequence;
    quintptr m_hwnd = 0;
    bool m_registered = false;
    int m_id = 0x4C4C; // 'LL'
};
