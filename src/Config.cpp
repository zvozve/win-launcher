#include "Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include "HotkeyManager.h"

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
    m_followSystemTheme = o.value(QStringLiteral("followSystemTheme")).toBool(true);
    m_darkMode = o.value(QStringLiteral("darkMode")).toBool(false);
    m_gridColumns = o.value(QStringLiteral("gridColumns")).toInt(7);
    m_appCategory = o.value(QStringLiteral("appCategory")).toString(QStringLiteral("all"));
    m_groupThreshold = o.value(QStringLiteral("groupThreshold")).toInt(2);

    /*
     * v1 → v2：归组阈值默认值 3 → 2。
     *
     * 老配置里存的是 3，直接改默认值对已装用户不生效 —— 而「开始菜单里只有 2 个
     * 入口的目录」（火绒安全软件 + 火绒日志这种）看着就是个没折起来的文件夹。
     * 只在「值仍等于旧默认 3」时迁移，用户手动改成别的值一律尊重。
     */
    if (o.value(QStringLiteral("version")).toInt(1) < 2 && m_groupThreshold == 3) {
        m_groupThreshold = 2;
        save();
    }

    // 以注册表为准，避免用户手工改动后配置与系统不一致
    m_autostart = isAutostartEnabled();
    syncDarkMode();
}

QJsonObject Config::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 2);
    o.insert(QStringLiteral("hotkey"), m_hotkey);
    o.insert(QStringLiteral("autostart"), m_autostart);
    o.insert(QStringLiteral("hideOnFocusOut"), m_hideOnFocusOut);
    o.insert(QStringLiteral("includeRegistry"), m_includeRegistry);
    o.insert(QStringLiteral("followSystemTheme"), m_followSystemTheme);
    o.insert(QStringLiteral("darkMode"), m_darkMode);
    o.insert(QStringLiteral("gridColumns"), m_gridColumns);
    o.insert(QStringLiteral("appCategory"), m_appCategory);
    o.insert(QStringLiteral("groupThreshold"), m_groupThreshold);
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

QStringList Config::categoryKeys()
{
    return {QStringLiteral("app"),     QStringLiteral("doc"),  QStringLiteral("image"),
            QStringLiteral("video"),   QStringLiteral("audio"), QStringLiteral("archive"),
            QStringLiteral("all")};
}

QStringList Config::categoryLabels()
{
    return {QStringLiteral("应用"), QStringLiteral("文档"), QStringLiteral("图片"),
            QStringLiteral("视频"), QStringLiteral("音乐"), QStringLiteral("压缩包"),
            QStringLiteral("全部")};
}

QString Config::categoryLabel(const QString &key)
{
    const QStringList keys = categoryKeys();
    const QStringList labels = categoryLabels();
    const int idx = keys.indexOf(key);
    return idx >= 0 ? labels.at(idx) : labels.first();
}

QString Config::categoryExtensions(const QString &key)
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("app"),
         QStringLiteral("exe;lnk;url;bat;cmd;com;msc")},
        {QStringLiteral("doc"),
         QStringLiteral("txt;md;log;pdf;doc;docx;xls;xlsx;ppt;pptx;csv;json;xml;yaml;yml;ini")},
        {QStringLiteral("image"),
         QStringLiteral("jpg;jpeg;png;gif;bmp;webp;svg;ico;tif;tiff;heic;avif;psd")},
        {QStringLiteral("video"),
         QStringLiteral("mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg;ts;rmvb;3gp")},
        {QStringLiteral("audio"),
         QStringLiteral("mp3;flac;wav;aac;m4a;ogg;oga;wma;ape;opus;mid")},
        {QStringLiteral("archive"),
         QStringLiteral("zip;rar;7z;tar;gz;bz2;xz;zst;iso;cab;lz4;tgz")},
        {QStringLiteral("all"), QString()},
    };
    return table.value(key);
}

QString Config::expandTypeToken(const QString &token)
{
    const QString t = token.trimmed().toLower();
    if (t.isEmpty())
        return QString();

    const QStringList keys = categoryKeys();
    const QStringList labels = categoryLabels();
    QString key;
    const int ki = keys.indexOf(t);
    if (ki >= 0)
        key = keys.at(ki);
    else {
        const int li = labels.indexOf(t);
        if (li >= 0)
            key = keys.at(li);
    }
    if (key.isEmpty())
        return QString();          // 不是类型词
    if (key == QStringLiteral("all"))
        return QStringLiteral("*"); // 「全部 / 所有类型」：不限制扩展名
    return categoryExtensions(key);
}

void Config::setAppCategory(const QString &v)
{
    if (m_appCategory == v)
        return;
    m_appCategory = v;
    emit appCategoryChanged();
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

/*
 * 归组阈值。下限 2：1 就等于「每个条目各自成组」，那不是归组是折腾。
 * 上限 12：再高基本就没有组会被折叠了。
 */
void Config::setGroupThreshold(int v)
{
    const int clamped = qBound(2, v, 12);
    if (m_groupThreshold == clamped)
        return;
    m_groupThreshold = clamped;
    emit groupThresholdChanged();
    save();
}
