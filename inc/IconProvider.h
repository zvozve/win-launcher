#pragma once

#include <QString>

/*
 * 图标提取与缓存。
 *
 * 路线：Shell 系统图像列表（SHIL_JUMBO，256x256）优先，失败回退 QFileIconProvider。
 * 结果统一落盘为 PNG，key = 目标路径 + mtime + size 的哈希，源文件变化会自动重提。
 */
class IconProvider
{
public:
    // 返回缓存 PNG 的绝对路径；失败返回空字符串
    static QString iconForPath(const QString &path);

    // 缓存目录 %LOCALAPPDATA%/WinLauncher/cache/icons
    static QString cacheDir();

    // 清空缓存（编辑界面提供「重建图标」时用）
    static void clearCache();
};
