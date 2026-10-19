#include "AppScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

#include "AppItem.h"
#include "AppCategory.h"
#include "DebugTrace.h"
#include "IconProvider.h"
#include "ShortcutResolver.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

namespace {

/*
 * 明显是「卸载 / 修复 / 驱动包 / 系统补丁」的条目，直接不收。
 *
 * Windows 的开始菜单和注册表里这类东西特别多：每装一个驱动就留下一条
 * "Windows Driver Package - XXX"，每个软件都有 Uninstall 快捷方式。它们不是
 * 用户想「启动」的应用，macOS 的启动台里也看不到这些入口。
 */
bool isNoiseName(const QString &name)
{
    static const QStringList kNoise = {
        QStringLiteral("uninstall"), QStringLiteral("unins"),
        QStringLiteral("卸载"),       QStringLiteral("移除"),
        QStringLiteral("删除"),       QStringLiteral("repair"),
        QStringLiteral("修复"),       QStringLiteral("windows driver package"),
        QStringLiteral("驱动程序包"),  QStringLiteral("update for "),
        QStringLiteral("security update"), QStringLiteral("hotfix"),
    };

    const QString lower = name.toLower();
    for (const QString &k : kNoise) {
        if (lower.contains(k))
            return true;
    }
    return false;
}

/*
 * 只收「能启动的东西」。
 *
 * 开始菜单里常有点向 .pdf / .chm / .txt 的快捷方式（用户手册、更新日志之类），
 * 解出目标后如果不看类型就会变成一个点了没反应的图标 —— 之前 PDF 混进来就是这么来的。
 * 解析不出目标（空串）的情况不在这里判断，那是 UWP / shell 命名空间项，另走一条路。
 */
bool isLaunchableTarget(const QString &target)
{
    static const QStringList kExts = {
        QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("bat"),
        QStringLiteral("cmd"), QStringLiteral("msc"), QStringLiteral("ps1"),
    };
    return kExts.contains(QFileInfo(target).suffix().toLower());
}

/*
 * 目标本身是「卸载程序」的条目，不收。
 *
 * 注册表 Uninstall 项的 DisplayIcon 有相当一部分直接指向软件自带的卸载器。
 * 只看 DisplayName 拦不住 —— 实测本机：
 *   - 嘉立创EDA(专业版) 3.2.149 → C:\Program Files\lceda-pro\unins000.exe
 *   - MSYS2                  → C:\msys64\uninstall.exe
 * 名字都是正常软件名，图标却是卸载器，于是在启动台里表现为
 * 「和文件夹同名 / 多带一个版本号的重复图标」，点下去弹的是卸载确认框。
 */
bool isUninstallerTarget(const QString &path)
{
    /*
     * 路径本身在「卸载信息」目录里的也算：本机 Canon 那条
     * DisplayName=「Canon iR C3125」、目标是
     * C:\Windows\system32\CanonMF Uninstaller Information\{...}\misc\DelDrv.exe，
     * 只看文件名（DelDrv）和显示名都拦不住，但目录名已经说明它是什么了。
     */
    if (path.contains(QLatin1String("uninstaller"), Qt::CaseInsensitive)
        || path.contains(QLatin1String("\\uninstall"), Qt::CaseInsensitive))
        return true;

    const QString base = QFileInfo(path).completeBaseName().toLower();
    static const QStringList kPrefixes = {
        QStringLiteral("unins"),   // unins000 / uninstall / uninst
        QStringLiteral("unwise"),  // InstallShield 的老卸载器
        QStringLiteral("uninst"),
    };
    for (const QString &p : kPrefixes) {
        if (base.startsWith(p))
            return true;
    }
    return false;
}

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

/*
 * 开始菜单里那些「Windows 自带」的文件夹不该被折叠。
 *
 * 不是洁癖：辅助功能 5 个、管理工具 20 个都是平时要一个个点的系统工具，
 * 折进文件夹反而多一次点击；而 J-Link / MSYS2 / NI 那种一堆入口的才需要收。
 * 放在文件夹这一层判断比在关键词层做干净（AppCategory 的 utility 规则就是被这批
 * 逼出来的）。比较用小写。
 */
bool isSystemFolderGroup(const QString &group)
{
    static const QSet<QString> kBlocked = {
        QStringLiteral("accessibility"),
        QStringLiteral("administrative tools"),
        QStringLiteral("windows administrative tools"),
        QStringLiteral("windows 管理工具"),
        QStringLiteral("辅助功能"),
        QStringLiteral("附件"),
        QStringLiteral("accessories"),
        QStringLiteral("startup"),
        QStringLiteral("启动"),
        QStringLiteral("系统工具"),
        QStringLiteral("system tools"),
    };
    return kBlocked.contains(group.toLower());
}

void appendStartMenuTasks(const QString &root, QList<AppScanner::Task> *out)
{
    if (root.isEmpty())
        return;

    const QDir rootDir(root);

    // 只收 .lnk。.url 是「网站快捷方式」（官网 / 文档 / 支持论坛 / mailto），
    // 开始菜单里每个软件都附带一堆，macOS 的启动台也不会把它们列出来。
    QDirIterator it(root,
                    {QStringLiteral("*.lnk")},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString baseName = QFileInfo(path).completeBaseName();
        // 名字里就写着卸载的，连图标都不用去提
        if (isNoiseName(baseName))
            continue;

        /*
         * 归组键 = 快捷方式所在目录相对开始菜单根的那一段。
         * 直接躺在开始菜单根下的（"Git\Git Bash.lnk" 的父目录就是根）
         * 相对路径是 "." → 不归组，保持原来的平铺。
         */
        const QString rel = QDir::cleanPath(
            rootDir.relativeFilePath(QFileInfo(path).absolutePath()));
        const bool inSubFolder = !rel.isEmpty() && rel != QLatin1String(".");

        AppScanner::Task task;
        task.kind = AppScanner::Task::Link;
        task.path = QFileInfo(path).absoluteFilePath();
        task.name = baseName;
        task.seed = task.path.toLower();
        task.group = inSubFolder ? rel : QString();
        if (isSystemFolderGroup(task.group))
            task.group.clear();
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
            // ParentKeyName / ReleaseType 非空 = 这是某个软件的子组件或系统补丁
            // （驱动包、Windows Update 项都是），不是一个能独立启动的应用
            const QString parentKey = reg.value(QStringLiteral("ParentKeyName")).toString();
            const QString releaseType = reg.value(QStringLiteral("ReleaseType")).toString();
            reg.endGroup();

            if (displayName.isEmpty() || systemComponent == 1)
                continue;
            if (!parentKey.isEmpty() || !releaseType.isEmpty())
                continue;
            if (displayIconRaw.isEmpty() || isNoiseName(displayName))
                continue;

            const QString iconPath = normalizeDisplayIcon(displayIconRaw);
            if (!iconPath.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                continue;
            if (!QFileInfo::exists(iconPath))
                continue;
            // 图标指向卸载器 = 这条注册表项其实是「卸载入口」（见 isUninstallerTarget）
            if (isUninstallerTarget(iconPath))
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
    app.group = task.group;

    if (task.kind == Task::Executable) {
        app.name = task.name;
        app.kind = AppItem::KindDesktop;
        app.target = task.path;
        app.workingDir = QFileInfo(task.path).absolutePath();
        app.sourcePath = task.path;
        app.category = AppCategory::classify(app.name, app.target);
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
        // 兜底：正常不会走到这里（buildTasks 阶段就不收 .url 了）。
        // 留一条是因为手动拖入的 .url 也走 runTask —— 那种按「用户明确指定」处理会更合理，
        // 但当前 UI 只允许拖 .lnk/.exe，所以先一并挡掉。
        wlTrace(QStringLiteral("scan: skip url shortcut -> ") + task.path);
        return false;
    } else if (info.valid) {
        // 目标不是可执行文件的（开始菜单里常见的「用户手册」指向 .pdf / .chm / .txt），
        // 收进来就是一个点了没反应的图标 —— 之前 PDF 混进列表就是这么来的。
        if (!isLaunchableTarget(info.target)) {
            wlTrace(QStringLiteral("scan: skip non-launchable -> ") + info.target);
            return false;
        }
        // 快捷方式名字没写「卸载」但目标就是卸载器（少见，兜一层）
        if (isUninstallerTarget(info.target)) {
            wlTrace(QStringLiteral("scan: skip uninstaller -> ") + info.target);
            return false;
        }
        app.kind = AppItem::KindDesktop;
        app.target = info.target;
        app.arguments = info.arguments;
        app.workingDir = info.workingDir;
    } else {
        // 解析不出目标（目标已卸载 / shell 命名空间项），保留条目但标为 manual
        if (isNoiseName(app.name))
            return false;
        app.kind = AppItem::KindManual;
    }

    app.category = AppCategory::classify(app.name, app.target);
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
    item->setCategory(data.category);
    item->setGroup(data.group);
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

    /*
     * ★ 普通文件（pdf / xlsx / docx / png…）也要能造出条目。
     *
     * 全盘搜索（Everything）命中的就是这些文件；以前这里对「非 lnk / 非 exe」
     * 一律 return nullptr，于是 Controller 把它们全丢掉 —— Everything 明明查到
     * 8 个「1.4 pdf」，界面却写「全盘没有找到」（2026-10-19 实测定位）。
     *
     * 这类条目没有可执行目标，走 KindManual：Launcher 里 QProcess 起不来会退到
     * ShellExecute("open")，由系统用默认程序打开。图标由 SHGetFileInfo 提供，
     * 就是资源管理器里那个 pdf / xlsx 图标。
     */
    const bool link = ShortcutResolver::isLinkFile(clean) || ShortcutResolver::isUrlFile(clean);
    const bool exe = fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0;
    if (!link && !exe) {
        ScannedApp data;
        data.name = fi.completeBaseName();
        data.kind = AppItem::KindManual;
        data.target = fi.absoluteFilePath();
        data.workingDir = fi.absolutePath();
        data.sourcePath = fi.absoluteFilePath();
        data.category = AppCategory::classify(data.name, data.target);
        data.iconPath = IconProvider::iconForPath(data.target);
        data.seed = fi.absoluteFilePath().toLower(); // 同一文件多轮搜索得到同一个 id
        data.removable = false;
        return toItem(data, parent);
    }

    Task task;
    task.path = fi.absoluteFilePath();
    task.name = fi.completeBaseName();

    if (link) {
        task.kind = Task::Link;
        task.seed = clean.toLower();
    } else {
        task.kind = Task::Executable;
        task.name = fi.completeBaseName();
        task.seed = fi.absoluteFilePath().toLower();
    }

    ScannedApp data;
    if (!runTask(task, &data))
        return nullptr;

    AppItem *item = toItem(data, parent);
    item->setRemovable(true); // 手动拖入的条目允许删除
    return item;
}
