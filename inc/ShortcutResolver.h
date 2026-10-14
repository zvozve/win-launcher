#pragma once

#include <QString>

/*
 * .lnk / .url 快捷方式解析。
 * .lnk 通过 COM (IShellLinkW + IPersistFile) 解析出真实目标与参数；
 * .url 是 INI 格式，直接读 [InternetShortcut] 段的 URL= 。
 */
struct ShortcutInfo
{
    bool valid = false;
    QString target;        // 解析后的可执行文件 / URL
    QString arguments;
    QString workingDir;
    QString iconLocation;  // 可能为空
    QString description;
};

class ShortcutResolver
{
public:
    static bool isLinkFile(const QString &path);
    static bool isUrlFile(const QString &path);
    static ShortcutInfo resolve(const QString &linkPath);
};
