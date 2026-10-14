#include "Launcher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#include "AppItem.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

bool launchUwp(const QString &aumid, QString *error)
{
    IApplicationActivationManager *manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr) || !manager) {
        if (error)
            *error = QStringLiteral("无法创建应用激活管理器 (0x%1)")
                         .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    // 允许把前台焦点交给被拉起的应用
    CoAllowSetForegroundWindow(manager, nullptr);

    DWORD pid = 0;
    hr = manager->ActivateApplication(aumid.toStdWString().c_str(), nullptr, AO_NONE, &pid);
    manager->Release();

    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("激活商店应用失败 (0x%1)")
                         .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool launchShell(const QString &target, const QString &args, const QString &workingDir, QString *error)
{
    const std::wstring wTarget = target.toStdWString();
    const std::wstring wArgs = args.toStdWString();
    const std::wstring wDir = workingDir.toStdWString();

    HINSTANCE r = ShellExecuteW(nullptr,
                                L"open",
                                wTarget.c_str(),
                                args.isEmpty() ? nullptr : wArgs.c_str(),
                                workingDir.isEmpty() ? nullptr : wDir.c_str(),
                                SW_SHOWNORMAL);
    if (reinterpret_cast<qintptr>(r) <= 32) {
        if (error)
            *error = QStringLiteral("ShellExecute 失败 (code %1)").arg(reinterpret_cast<qintptr>(r));
        return false;
    }
    return true;
}

} // namespace

bool Launcher::launch(AppItem *item, QString *error)
{
    if (!item) {
        if (error)
            *error = QStringLiteral("条目为空");
        return false;
    }

    if (item->isUwp())
        return launchUwp(item->aumid(), error);

    const QString target = item->target();
    if (target.isEmpty()) {
        if (error)
            *error = QStringLiteral("该条目没有可执行目标：%1").arg(item->name());
        return false;
    }

    if (item->kind() == AppItem::KindUrl || target.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        return launchShell(target, QString(), QString(), error);

    // 普通 exe：QProcess 更干净，失败再退到 ShellExecute（能处理 .bat / 关联程序）
    const QString program = target;
    const QStringList arguments =
        QProcess::splitCommand(item->arguments());
    if (!QProcess::startDetached(program, arguments, item->workingDir()))
        return launchShell(target, item->arguments(), item->workingDir(), error);

    return true;
}

bool Launcher::runAsAdmin(AppItem *item, QString *error)
{
    if (!item) {
        if (error)
            *error = QStringLiteral("条目为空");
        return false;
    }

    if (item->isUwp()) {
        if (error)
            *error = QStringLiteral("商店应用不支持提权启动");
        return false;
    }

    const QString target = item->target();
    if (target.isEmpty()) {
        if (error)
            *error = QStringLiteral("该条目没有可执行目标：%1").arg(item->name());
        return false;
    }

    const QString args = item->arguments();
    const QString dir = item->workingDir();

    const std::wstring wTarget = QDir::toNativeSeparators(target).toStdWString();
    const std::wstring wArgs = args.toStdWString();
    const std::wstring wDir = QDir::toNativeSeparators(dir).toStdWString();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // NO_UI：让"找不到文件"这类错误走我们自己的提示条，而不是弹系统对话框。
    // NOCLOSEPROCESS 只是为了拿到 hProcess 立刻关掉，不等它退出。
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = wTarget.c_str();
    sei.lpParameters = args.isEmpty() ? nullptr : wArgs.c_str();
    sei.lpDirectory = dir.isEmpty() ? nullptr : wDir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (error) {
            *error = (err == ERROR_CANCELLED)
                         ? QStringLiteral("已取消提权")
                         : QStringLiteral("提权启动失败 (code %1)").arg(err);
        }
        return false;
    }

    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    return true;
}

bool Launcher::revealInExplorer(AppItem *item, QString *error)
{
    if (!item) {
        if (error)
            *error = QStringLiteral("条目为空");
        return false;
    }

    // 优先定位快捷方式本体（用户拖进来/开始菜单里那个文件），没有就退回目标
    QString path = item->sourcePath();
    if (path.isEmpty())
        path = item->target();
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("该条目没有可定位的路径");
        return false;
    }

    path = QDir::toNativeSeparators(path);
    if (!QFileInfo::exists(path)) {
        // 文件已经没了就退到它所在的目录，别直接失败
        const QString dir = QFileInfo(path).absolutePath();
        if (QFileInfo::exists(dir))
            path = dir;
    }

    // 用 SHOpenFolderAndSelectItems 而不是 explorer.exe /select：
    // 后者要自己拼引号，路径里有空格 / 逗号时容易翻车，这个是 shell 官方接口。
    const std::wstring wide = path.toStdWString();
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(wide.c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl) {
        if (error)
            *error = QStringLiteral("路径无法解析：%1").arg(path);
        return false;
    }

    hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);

    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("打开资源管理器失败 (0x%1)")
                         .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}
