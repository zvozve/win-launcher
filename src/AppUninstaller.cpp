#include "AppUninstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

#include "AppItem.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

// DisplayIcon 形如 "C:\App\app.exe,0"，也可能带引号 / 环境变量
QString normalizeIconPath(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('"'))) {
        const int end = s.indexOf(QLatin1Char('"'), 1);
        if (end > 0)
            s = s.mid(1, end - 1);
    } else {
        const int comma = s.lastIndexOf(QLatin1Char(','));
        if (comma > 0 && !s.contains(QLatin1Char(' ')))
            s = s.left(comma);
    }
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));

    if (s.contains(QLatin1Char('%'))) {
        const std::wstring wide = s.toStdWString();
        wchar_t buf[MAX_PATH * 2] = {0};
        if (ExpandEnvironmentStringsW(wide.c_str(), buf, MAX_PATH * 2) > 0 && buf[0])
            s = QString::fromWCharArray(buf);
    }
    return QDir::cleanPath(s.trimmed());
}

QString norm(const QString &s)
{
    return QDir::cleanPath(s.trimmed()).toLower();
}

/*
 * 读一遍三个根的 Uninstall 项。
 *
 * 不缓存：键只有两三百个，一次遍历几十毫秒；缓存反而会在用户刚卸载完一个软件后
 * 给出陈旧结果。
 */
QList<AppUninstaller::Entry> readEntries()
{
    const QStringList roots = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    };

    QList<AppUninstaller::Entry> list;

    for (const QString &root : roots) {
        QSettings reg(root, QSettings::NativeFormat);
        const QStringList groups = reg.childGroups();
        for (const QString &g : groups) {
            reg.beginGroup(g);
            const QString displayName = reg.value(QStringLiteral("DisplayName")).toString();
            const QString uninstall = reg.value(QStringLiteral("UninstallString")).toString();
            const QString iconRaw = reg.value(QStringLiteral("DisplayIcon")).toString();
            const QString installLoc = reg.value(QStringLiteral("InstallLocation")).toString();
            reg.endGroup();

            if (uninstall.isEmpty())
                continue;

            AppUninstaller::Entry e;
            e.key = root + QLatin1Char('\\') + g;
            e.displayName = displayName;
            e.displayIcon = iconRaw.isEmpty() ? QString() : normalizeIconPath(iconRaw);
            e.uninstallString = uninstall;
            e.installLocation = installLoc;
            list.append(e);
        }
    }

    return list;
}

// 参数里带空格就得加引号，否则 ShellExecute 会把一个参数拆成两个
QString quoteIfNeeded(const QString &arg)
{
    if (arg.isEmpty()
        || arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'))) {
        return QLatin1Char('"') + arg + QLatin1Char('"');
    }
    return arg;
}

} // namespace

namespace AppUninstaller {

Entry findFor(const AppItem *item)
{
    if (!item)
        return {};

    const QString target = norm(item->target());
    const QString name = item->name().trimmed();
    // 丢掉 " (x86)" / " (64-bit)" 这类尾缀再比一次名字：快捷方式的名字常带这些，
    // 而注册表 DisplayName 未必带。
    const QString bareName = QString(name).remove(QRegularExpression(
        QStringLiteral("\\s*[\\(（][^)）]*[\\)）]\\s*$")));
    const QList<Entry> all = readEntries();

    // 1) 图标路径就是目标 exe（扫描注册表时也是按这个收进来的，最可靠）
    if (!target.isEmpty()) {
        for (const Entry &e : all) {
            if (!e.displayIcon.isEmpty() && norm(e.displayIcon) == target)
                return e;
        }

        // 2) 目标落在 InstallLocation 里
        for (const Entry &e : all) {
            if (e.installLocation.isEmpty())
                continue;
            const QString loc = norm(e.installLocation);
            if (loc.isEmpty())
                continue;
            if (target == loc || target.startsWith(loc + QLatin1Char('/')))
                return e;
        }

        /*
         * 2b) 目标落在 DisplayIcon 所在目录里。
         *
         * 很多卸载项压根没有 InstallLocation（Git for Windows 就是），
         * 但 DisplayIcon 指向安装目录里的某个 exe，拿它反推目录很可靠。
         * 长度下限是为了别让 "C:/" 这种根目录把一切都匹配掉。
         */
        for (const Entry &e : all) {
            if (e.displayIcon.isEmpty())
                continue;
            const QString dir = norm(QFileInfo(e.displayIcon).absolutePath());
            if (dir.length() < 4)
                continue;
            if (target.startsWith(dir + QLatin1Char('/')))
                return e;
        }
    }

    if (name.isEmpty())
        return {};

    // 3) 显示名完全相同
    for (const Entry &e : all) {
        if (!e.displayName.isEmpty() && e.displayName.compare(name, Qt::CaseInsensitive) == 0)
            return e;
    }

    // 4) 显示名包含条目名（快捷方式常把厂商前缀省掉，比如 "Visual Studio Code"
    //    对应的 DisplayName 是 "Microsoft Visual Studio Code"）
    for (const Entry &e : all) {
        if (!e.displayName.isEmpty() && e.displayName.contains(name, Qt::CaseInsensitive))
            return e;
    }

    // 5) 去掉尾部括号再试一次（"Python 3.12 (64-bit)" -> "Python 3.12"）
    if (!bareName.isEmpty() && bareName.compare(name, Qt::CaseInsensitive) != 0) {
        for (const Entry &e : all) {
            if (!e.displayName.isEmpty()
                && e.displayName.contains(bareName, Qt::CaseInsensitive)) {
                return e;
            }
        }
    }

    return {};
}

bool run(const Entry &entry, QString *error)
{
    if (!entry.isValid()) {
        if (error)
            *error = QStringLiteral("找不到这个程序的卸载入口");
        return false;
    }

    /*
     * UninstallString 有两种常见形态：
     *   "C:\Program Files\Foo\uninstall.exe" /S
     *   MsiExec.exe /X{0123-...}
     * 先按命令行规则拆成 exe + 参数，再交给 ShellExecuteEx —— 直接把它整个塞进
     * lpFile 是不行的（前者带引号、后者根本没有路径）。
     */
    const std::wstring cmd = entry.uninstallString.toStdWString();
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(cmd.c_str(), &argc);
    if (!argv || argc == 0) {
        if (argv)
            LocalFree(argv);
        if (error)
            *error = QStringLiteral("卸载命令无法解析：%1").arg(entry.uninstallString);
        return false;
    }

    const QString file = QString::fromWCharArray(argv[0]);
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromWCharArray(argv[i]);
    LocalFree(argv);

    if (file.isEmpty()) {
        if (error)
            *error = QStringLiteral("卸载命令里没有可执行文件");
        return false;
    }

    const std::wstring wFile = file.toStdWString();
    const std::wstring wArgs = args.join(QLatin1Char(' ')).toStdWString();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = wFile.c_str();
    sei.lpParameters = args.isEmpty() ? nullptr : wArgs.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (error) {
            *error = (err == ERROR_CANCELLED)
                         ? QStringLiteral("已取消")
                         : QStringLiteral("启动卸载程序失败 (code %1)").arg(err);
        }
        return false;
    }
    return true;
}

} // namespace AppUninstaller
