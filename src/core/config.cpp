#include "core/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include "core/hotkeymanager.h"

namespace {

const QString kRunKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRunValue = QStringLiteral("WinLauncher");
const QString kThemeKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
const QString kAutostartArg = QStringLiteral("--autostart");

} // namespace

Config::Config(QObject *parent)
    : QObject(parent)
    , m_hotkey(HotkeyManager::defaultSequence())
{
    m_darkMode = systemUsesDarkMode();
}

QString Config::configPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

void Config::load()
{
    QFile f(configPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        // 首次运行：把注册表里真实的开机自启状态同步进来
        m_autostart = isAutostartEnabled();
        syncDarkMode();
        save();
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    m_hotkey = o.value(QStringLiteral("hotkey")).toString(m_hotkey);
    m_autostart = o.value(QStringLiteral("autostart")).toBool(false);
    m_hideOnFocusOut = o.value(QStringLiteral("hideOnFocusOut")).toBool(true);
    m_includeRegistry = o.value(QStringLiteral("includeRegistry")).toBool(true);
    m_searchSource = o.value(QStringLiteral("searchSource")).toString(QStringLiteral("local"));
    m_followSystemTheme = o.value(QStringLiteral("followSystemTheme")).toBool(true);
    m_darkMode = o.value(QStringLiteral("darkMode")).toBool(false);
    m_gridColumns = o.value(QStringLiteral("gridColumns")).toInt(7);

    // 以注册表为准，避免用户手工改动后配置与系统不一致
    m_autostart = isAutostartEnabled();
    syncDarkMode();
}

QJsonObject Config::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("hotkey"), m_hotkey);
    o.insert(QStringLiteral("autostart"), m_autostart);
    o.insert(QStringLiteral("hideOnFocusOut"), m_hideOnFocusOut);
    o.insert(QStringLiteral("includeRegistry"), m_includeRegistry);
    o.insert(QStringLiteral("searchSource"), m_searchSource);
    o.insert(QStringLiteral("followSystemTheme"), m_followSystemTheme);
    o.insert(QStringLiteral("darkMode"), m_darkMode);
    o.insert(QStringLiteral("gridColumns"), m_gridColumns);
    return o;
}

void Config::save()
{
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    f.close();
}

void Config::syncDarkMode()
{
    if (m_followSystemTheme) {
        const bool dark = systemUsesDarkMode();
        if (dark != m_darkMode) {
            m_darkMode = dark;
            emit darkModeChanged();
        }
    }
}

bool Config::systemUsesDarkMode()
{
    QSettings reg(kThemeKey, QSettings::NativeFormat);
    return reg.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
}

bool Config::isAutostartEnabled()
{
    QSettings reg(kRunKey, QSettings::NativeFormat);
    if (!reg.contains(kRunValue))
        return false;
    const QString stored = reg.value(kRunValue).toString();
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return stored.contains(exe, Qt::CaseInsensitive);
}

void Config::setAutostartEnabled(bool on)
{
    QSettings reg(kRunKey, QSettings::NativeFormat);
    if (on) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        reg.setValue(kRunValue, QStringLiteral("\"%1\" %2").arg(exe, kAutostartArg));
    } else {
        reg.remove(kRunValue);
    }
    reg.sync();
}

// ---- setters ---------------------------------------------------------------

void Config::setHotkey(const QString &v)
{
    if (m_hotkey == v)
        return;
    m_hotkey = v;
    emit hotkeyChanged();
    save();
}

void Config::setAutostart(bool v)
{
    if (m_autostart == v)
        return;
    m_autostart = v;
    setAutostartEnabled(v);
    emit autostartChanged();
    save();
}

void Config::setHideOnFocusOut(bool v)
{
    if (m_hideOnFocusOut == v)
        return;
    m_hideOnFocusOut = v;
    emit hideOnFocusOutChanged();
    save();
}

void Config::setIncludeRegistry(bool v)
{
    if (m_includeRegistry == v)
        return;
    m_includeRegistry = v;
    emit includeRegistryChanged();
    save();
}

void Config::setSearchSource(const QString &v)
{
    if (m_searchSource == v)
        return;
    m_searchSource = v;
    emit searchSourceChanged();
    save();
}

void Config::setDarkMode(bool v)
{
    if (m_darkMode == v)
        return;
    m_darkMode = v;
    emit darkModeChanged();
    save();
}

void Config::setFollowSystemTheme(bool v)
{
    if (m_followSystemTheme == v)
        return;
    m_followSystemTheme = v;
    emit followSystemThemeChanged();
    syncDarkMode();
    save();
}

void Config::setGridColumns(int v)
{
    const int clamped = qBound(3, v, 12);
    if (m_gridColumns == clamped)
        return;
    m_gridColumns = clamped;
    emit gridColumnsChanged();
    save();
}
