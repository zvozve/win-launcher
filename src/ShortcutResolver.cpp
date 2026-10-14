#include "ShortcutResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

constexpr int kMaxPathChars = 4096;

// .lnk -> ShortcutInfo
ShortcutInfo resolveLnk(const QString &linkPath)
{
    ShortcutInfo info;

    IShellLinkW *link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (FAILED(hr) || !link)
        return info;

    IPersistFile *persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist));
    if (FAILED(hr) || !persist) {
        link->Release();
        return info;
    }

    const std::wstring wide = QDir::toNativeSeparators(linkPath).toStdWString();
    hr = persist->Load(wide.c_str(), STGM_READ);
    if (SUCCEEDED(hr)) {
        wchar_t buf[kMaxPathChars] = {0};

        // 注意：GetPath 带 WIN32_FIND_DATAW + SLGP_UNCPRIORITY 时，shell 会去
        // 解析目标（UWP/AppsFolder 这类 shell 命名空间项会让 shell32 直接崩）。
        // 用 SLGP_RAWPATH 只取链接里存的原路径，不碰文件系统。
        if (SUCCEEDED(link->GetPath(buf, kMaxPathChars, nullptr, SLGP_RAWPATH)) && buf[0]) {
            info.target = QString::fromWCharArray(buf);
            info.workingDir = QString::fromWCharArray(buf);
        }

        if (buf[0] == L'%') {
            wchar_t expanded[kMaxPathChars] = {0};
            if (ExpandEnvironmentStringsW(buf, expanded, kMaxPathChars) > 0 && expanded[0]) {
                info.target = QString::fromWCharArray(expanded);
                info.workingDir = QString::fromWCharArray(expanded);
            }
        }

        if (SUCCEEDED(link->GetArguments(buf, kMaxPathChars)))
            info.arguments = QString::fromWCharArray(buf);

        if (SUCCEEDED(link->GetWorkingDirectory(buf, kMaxPathChars)) && buf[0])
            info.workingDir = QString::fromWCharArray(buf);

        // 不调用 IShellLink::GetIconLocation：
        // 对目标是 shell 命名空间项（UWP / AppsFolder 之类）的快捷方式，
        // shell32 在内部解析图标位置时会直接段错误（Win11 + SDK 10.0.26100 实测）。
        // 图标统一由 AppScanner::pickIconSource 用 目标文件 -> 快捷方式本身 取，
        // 对绝大多数程序来说就是正确的那张图。
        // TODO: 若后续发现「图标与目标不一致」的条目，再评估带保护的替代方案。

        info.valid = !info.target.isEmpty();
    }

    persist->Release();
    link->Release();
    return info;
}

// .url -> ShortcutInfo（只需拿到 URL）
ShortcutInfo resolveUrl(const QString &urlPath)
{
    ShortcutInfo info;
    QSettings ini(urlPath, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("InternetShortcut"));
    const QString url = ini.value(QStringLiteral("URL")).toString();
    ini.endGroup();
    if (!url.isEmpty()) {
        info.target = url;
        info.valid = true;
    }
    return info;
}

} // namespace

bool ShortcutResolver::isLinkFile(const QString &path)
{
    return path.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive);
}

bool ShortcutResolver::isUrlFile(const QString &path)
{
    return path.endsWith(QLatin1String(".url"), Qt::CaseInsensitive);
}

ShortcutInfo ShortcutResolver::resolve(const QString &linkPath)
{
    if (isLinkFile(linkPath))
        return resolveLnk(linkPath);
    if (isUrlFile(linkPath))
        return resolveUrl(linkPath);

    // 直接指向 exe 的情况
    ShortcutInfo info;
    const QFileInfo fi(linkPath);
    if (fi.exists() && fi.isFile()) {
        info.target = fi.absoluteFilePath();
        info.workingDir = fi.absolutePath();
        info.valid = true;
    }
    return info;
}
