#include "Launcher.h"

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
