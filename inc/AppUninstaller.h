#pragma once

#include <QString>

class AppItem;

/*
 * 「卸载此程序」。
 *
 * 为什么不用 Geek Uninstaller 来卸：
 *   翻过 geek.exe 的字符串表 —— 里面只有 GetCommandLineA / GetCommandLineW /
 *   CommandLineToArgvW 这种 CRT 标配，没有任何 ParamStr / usage / -u 之类的命令行实现；
 *   官网也只提供一个单文件 exe，**没有 DLL / SDK**。也就是说它只能人肉点 UI，
 *   没法被程序调用。
 *
 * 所以走 Windows 自己的机制：从注册表的 Uninstall 键里读出这个程序的
 * UninstallString 再执行它 —— 这也是所有卸载工具底层做的事，通用、不挑软件。
 */
namespace AppUninstaller {

// 一条注册表卸载项
struct Entry
{
    QString key;              // 注册表键全路径（排查用）
    QString displayName;
    QString displayIcon;      // 已规范化（去掉 ",0" 后缀 / 引号）
    QString uninstallString;
    QString installLocation;

    bool isValid() const { return !uninstallString.isEmpty(); }
};

/*
 * 找出这个条目对应的注册表卸载项。依次按：
 *   1. DisplayIcon 路径 == 目标 exe
 *   2. 目标 exe 落在 InstallLocation 目录里
 *   3. DisplayName 与条目名完全相同
 *   4. DisplayName 包含条目名（模糊）
 * 都不中就返回 isValid() == false 的 Entry（绿色软件 / 商店应用本来就没有卸载项）。
 */
Entry findFor(const AppItem *item);

// 执行卸载。用 "open" 而不是 "runas"：卸载器自己的 manifest 需要管理员时系统会自己弹 UAC，
// 我们硬加 runas 的话，per-user 安装的程序会白弹一次提权。
bool run(const Entry &entry, QString *error = nullptr);

} // namespace AppUninstaller
