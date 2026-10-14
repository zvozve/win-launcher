#include "core/hotkeymanager.h"

#include <QKeySequence>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

constexpr UINT kModNoRepeat = 0x4000; // MOD_NOREPEAT

UINT toWinModifiers(Qt::KeyboardModifiers mods)
{
    UINT m = 0;
    if (mods & Qt::ControlModifier)
        m |= MOD_CONTROL;
    if (mods & Qt::AltModifier)
        m |= MOD_ALT;
    if (mods & Qt::ShiftModifier)
        m |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)
        m |= MOD_WIN;
    return m;
}

// Qt::Key -> Win32 虚拟键码。只覆盖实际会用作热键的键，其余返回 0。
UINT toVirtualKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<UINT>(key);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<UINT>(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));

    switch (key) {
    case Qt::Key_Space:     return VK_SPACE;
    case Qt::Key_Tab:       return VK_TAB;
    case Qt::Key_Backtab:   return VK_TAB;
    case Qt::Key_Return:    return VK_RETURN;
    case Qt::Key_Enter:     return VK_RETURN;
    case Qt::Key_Escape:    return VK_ESCAPE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete:    return VK_DELETE;
    case Qt::Key_Insert:    return VK_INSERT;
    case Qt::Key_Home:      return VK_HOME;
    case Qt::Key_End:       return VK_END;
    case Qt::Key_PageUp:    return VK_PRIOR;
    case Qt::Key_PageDown:  return VK_NEXT;
    case Qt::Key_Up:        return VK_UP;
    case Qt::Key_Down:      return VK_DOWN;
    case Qt::Key_Left:      return VK_LEFT;
    case Qt::Key_Right:     return VK_RIGHT;
    case Qt::Key_Print:     return VK_SNAPSHOT;
    case Qt::Key_Pause:     return VK_PAUSE;
    case Qt::Key_Comma:     return VK_OEM_COMMA;
    case Qt::Key_Period:    return VK_OEM_PERIOD;
    case Qt::Key_Slash:     return VK_OEM_2;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_QuoteLeft: return VK_OEM_3;
    case Qt::Key_Minus:     return VK_OEM_MINUS;
    case Qt::Key_Equal:     return VK_OEM_PLUS;
    case Qt::Key_BracketLeft:  return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Backslash:    return VK_OEM_5;
    default:                return 0;
    }
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , m_sequence(defaultSequence())
{
}

HotkeyManager::~HotkeyManager()
{
    release();
}

void HotkeyManager::attachWindow(quintptr hwnd)
{
    m_hwnd = hwnd;
    if (!apply()) {
        qWarning("HotkeyManager: 注册热键 \"%s\" 失败（可能已被其它程序占用）",
                 qPrintable(m_sequence));
    }
}

void HotkeyManager::release()
{
    if (m_registered && m_hwnd) {
        UnregisterHotKey(reinterpret_cast<HWND>(m_hwnd), m_id);
        m_registered = false;
        emit registeredChanged();
    }
}

bool HotkeyManager::apply()
{
    if (!m_hwnd)
        return false;

    release();

    const QKeySequence seq = QKeySequence::fromString(m_sequence, QKeySequence::PortableText);
    if (seq.isEmpty())
        return false;

    const QKeyCombination combo = seq[0];
    const UINT mods = toWinModifiers(combo.keyboardModifiers());
    const UINT vk = toVirtualKey(static_cast<int>(combo.key()));
    if (vk == 0 || mods == 0)
        return false;

    if (!RegisterHotKey(reinterpret_cast<HWND>(m_hwnd), m_id, mods | kModNoRepeat, vk))
        return false;

    m_registered = true;
    emit registeredChanged();
    return true;
}

bool HotkeyManager::setSequence(const QString &sequence, QString *error)
{
    const QString trimmed = sequence.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("热键不能为空");
        return false;
    }
    if (trimmed == m_sequence && m_registered)
        return true;

    const QString previous = m_sequence;
    m_sequence = trimmed;

    if (m_hwnd && !apply()) {
        m_sequence = previous;
        if (m_hwnd)
            apply(); // 尽量恢复原热键
        if (error)
            *error = QStringLiteral("热键 %1 注册失败，可能已被占用").arg(trimmed);
        emit sequenceChanged();
        return false;
    }

    emit sequenceChanged();
    return true;
}

bool HotkeyManager::handleNativeMessage(quintptr hwnd, unsigned int message, quintptr wParam)
{
    Q_UNUSED(hwnd)
    if (message != WM_HOTKEY)
        return false;
    if (static_cast<int>(wParam) != m_id)
        return false;

    emit activated();
    return true;
}
