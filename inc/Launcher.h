#pragma once

#include <QString>

class AppItem;

/*
 * 应用启动。
 *   UWP  -> IApplicationActivationManager::ActivateApplication(AUMID)
 *   .url  -> ShellExecuteW（交给默认浏览器）
 *   其他  -> QProcess::startDetached（exe + 参数 + 工作目录）
 */
class Launcher
{
public:
    static bool launch(AppItem *item, QString *error = nullptr);
};
