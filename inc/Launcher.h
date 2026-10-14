#pragma once

#include <QString>

class AppItem;

/*
 * 应用启动 + 图标右键菜单里的几个动作。
 *
 *   launch()            UWP -> IApplicationActivationManager::ActivateApplication(AUMID)
 *                       .url -> ShellExecuteW（交给默认浏览器）
 *                       其他  -> QProcess::startDetached（exe + 参数 + 工作目录）
 *   runAsAdmin()        ShellExecuteExW + "runas"（弹 UAC）
 *   revealInExplorer()  SHOpenFolderAndSelectItems（在资源管理器中定位并选中）
 *
 * 都返回 bool，失败时把原因写进 *error，由 Controller 显示到底部提示条。
 */
class Launcher
{
public:
    static bool launch(AppItem *item, QString *error = nullptr);

    // 以管理员身份启动。会弹 UAC；用户点「否」时 error = "已取消提权"。
    static bool runAsAdmin(AppItem *item, QString *error = nullptr);

    // 在资源管理器里定位这个条目（优先快捷方式本体，其次目标文件）
    static bool revealInExplorer(AppItem *item, QString *error = nullptr);
};
