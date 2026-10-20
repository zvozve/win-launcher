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

    /*
     * 只按**扩展名**取系统关联图标（返回缓存 PNG 路径，失败为空）。
     *
     * 全盘搜索命中的普通文件（pdf / docx / png…）必须走这条，不能用 iconForPath：
     * iconForPath 交给 SHGetFileInfo 真实路径查询，会加载该类型的第三方 shell
     * 扩展（Adobe / 福昕的 pdf handler 之类），首次调用能卡几十秒 ——
     * 「搜 1.4 pdf 卡死」的根因。这里用 SHGFI_USEFILEATTRIBUTES 伪装成文件，
     * 只查注册表里的关联图标，不碰磁盘、不加载扩展，毫秒级。
     */
    static QString associatedIcon(const QString &ext);

    // 缓存目录 %LOCALAPPDATA%/WinLauncher/cache/icons
    static QString cacheDir();

    // 清空缓存（编辑界面提供「重建图标」时用）
    static void clearCache();
};
