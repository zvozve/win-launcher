#include "AppScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

#include "AppItem.h"
#include "DebugTrace.h"
#include "IconProvider.h"
#include "ShortcutResolver.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

namespace {

QString knownFolder(int csidl)
{
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, 0, buf)))
        return QString::fromWCharArray(buf);
    return QString();
}

// 挑一个「能取到图标的东西」：显式 iconLocation 优先，其次目标文件，最后快捷方式本身
QString pickIconSource(const ShortcutInfo &info, const QString &sourcePath)
{
    if (!info.iconLocation.isEmpty()) {
        const QString iconFile = info.iconLocation.section(QLatin1Char(','), 0, 0).trimmed();
        if (QFileInfo::exists(iconFile))
            return iconFile;
    }
    if (!info.target.isEmpty() && QFileInfo::exists(info.target))
        return info.target;
    return sourcePath;
}

// DisplayIcon 形如 "C:\App\app.exe,0"，可能带引号
QString normalizeDisplayIcon(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('"'))) {
        const int end = s.indexOf(QLatin1Char('"'), 1);
        if (end > 0)
            s = s.mid(1, end - 1);
    } else {
        const int comma = s.lastIndexOf(QLatin1Char(','));
        if (comma > 0 && !s.contains(QLatin1Char(' '))) // 只切 "x.exe,0" 这类无空格形式
            s = s.left(comma);
    }
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return s.trimmed();
}

void appendStartMenuTasks(const QString &root, QList<AppScanner::Task> *out)
{
    if (root.isEmpty())
        return;

    QDirIterator it(root,
                    {QStringLiteral("*.lnk"), QStringLiteral("*.url")},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        AppScanner::Task task;
        task.kind = AppScanner::Task::Link;
        task.path = QFileInfo(path).absoluteFilePath();
        task.name = QFileInfo(path).completeBaseName();
        task.seed = task.path.toLower();
        out->append(task);
    }
}

void appendRegistryTasks(QList<AppScanner::Task> *out, QSet<QString> *seenTargets)
{
    const QStringList roots = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    };

    for (const QString &root : roots) {
        QSettings reg(root, QSettings::NativeFormat);
        const QStringList groups = reg.childGroups();
        for (const QString &g : groups) {
            reg.beginGroup(g);
            const QString displayName = reg.value(QStringLiteral("DisplayName")).toString();
            const int systemComponent = reg.value(QStringLiteral("SystemComponent"), 0).toInt();
            const QString displayIconRaw = reg.value(QStringLiteral("DisplayIcon")).toString();
            reg.endGroup();

            if (displayName.isEmpty() || systemComponent == 1 || displayIconRaw.isEmpty())
                continue;

            const QString iconPath = normalizeDisplayIcon(displayIconRaw);
            if (!iconPath.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                continue;
            if (!QFileInfo::exists(iconPath))
                continue;

            const QString key = QDir::toNativeSeparators(iconPath).toLower();
            if (seenTargets->contains(key))
                continue;
            seenTargets->insert(key);

            AppScanner::Task task;
            task.kind = AppScanner::Task::Executable;
            task.path = iconPath;
            task.name = displayName;
            task.seed = key;
            out->append(task);
        }
    }
}

} // namespace

QList<AppScanner::Task> AppScanner::buildTasks(const Options &opt)
{
    QList<Task> tasks;

    if (opt.startMenu) {
        appendStartMenuTasks(knownFolder(CSIDL_PROGRAMS), &tasks);        // 当前用户
        appendStartMenuTasks(knownFolder(CSIDL_COMMON_PROGRAMS), &tasks); // 所有用户
    }
    const int startMenuCount = tasks.size();
    wlTrace(QStringLiteral("buildTasks: %1 start-menu entries").arg(startMenuCount));

    if (opt.registry) {
        QSet<QString> seen;
        appendRegistryTasks(&tasks, &seen);
        wlTrace(QStringLiteral("buildTasks: +%1 registry entries").arg(tasks.size() - startMenuCount));
    }

    return tasks;
}

bool AppScanner::runTask(const Task &task, ScannedApp *out)
{
    if (!out)
        return false;

    ScannedApp app;
    app.seed = task.seed;

    if (task.kind == Task::Executable) {
        app.name = task.name;
        app.kind = AppItem::KindDesktop;
        app.target = task.path;
        app.workingDir = QFileInfo(task.path).absolutePath();
        app.sourcePath = task.path;
        app.iconPath = IconProvider::iconForPath(task.path);
        *out = app;
        return true;
    }

    // Link（.lnk / .url）
    const QFileInfo fi(task.path);
    if (!fi.exists() || !fi.isFile())
        return false;

    const ShortcutInfo info = ShortcutResolver::resolve(task.path);

    app.name = task.name.isEmpty() ? fi.completeBaseName() : task.name;
    app.sourcePath = fi.absoluteFilePath();

    if (ShortcutResolver::isUrlFile(task.path)) {
        app.kind = AppItem::KindUrl;
        app.target = info.target;
    } else if (info.valid) {
        app.kind = AppItem::KindDesktop;
        app.target = info.target;
        app.arguments = info.arguments;
        app.workingDir = info.workingDir;
    } else {
        // 解析不出目标（目标已卸载 / shell 命名空间项），保留条目但标为 manual
        app.kind = AppItem::KindManual;
    }

    app.iconPath = IconProvider::iconForPath(pickIconSource(info, task.path));
    *out = app;
    return !app.name.isEmpty();
}

AppItem *AppScanner::toItem(const ScannedApp &data, QObject *parent)
{
    auto *item = new AppItem(parent);
    item->setName(data.name);
    item->setKind(data.kind);
    item->setTarget(data.target);
    item->setArguments(data.arguments);
    item->setWorkingDir(data.workingDir);
    item->setSourcePath(data.sourcePath);
    item->setIconPath(data.iconPath);
    item->setRemovable(data.removable);
    item->rebindId(data.seed.isEmpty() ? data.target.toLower() : data.seed);
    return item;
}

AppItem *AppScanner::fromAnyPath(const QString &path, QObject *parent)
{
    const QString clean = path.trimmed();
    if (clean.isEmpty())
        return nullptr;

    const QFileInfo fi(clean);
    if (!fi.exists() || !fi.isFile())
        return nullptr;

    Task task;
    task.path = fi.absoluteFilePath();
    task.name = fi.completeBaseName();

    if (ShortcutResolver::isLinkFile(clean) || ShortcutResolver::isUrlFile(clean)) {
        task.kind = Task::Link;
        task.seed = clean.toLower();
    } else if (fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0) {
        task.kind = Task::Executable;
        task.name = fi.completeBaseName();
        task.seed = fi.absoluteFilePath().toLower();
    } else {
        return nullptr;
    }

    ScannedApp data;
    if (!runTask(task, &data))
        return nullptr;

    AppItem *item = toItem(data, parent);
    item->setRemovable(true); // 手动拖入的条目允许删除
    return item;
}
