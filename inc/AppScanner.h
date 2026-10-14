#pragma once

#include <QList>
#include <QString>

class QObject;
class AppItem;

/*
 * 扫描结果（纯数据，不含 QObject）。
 */
struct ScannedApp
{
    QString name;
    QString kind;
    QString target;
    QString arguments;
    QString workingDir;
    QString sourcePath;
    QString iconPath;
    QString category;  // AppCategory::classify() 的结果，供启动台的分类标签筛选
    QString seed;      // 用于生成稳定 ID
    bool removable = false;
};

/*
 * 已安装应用扫描。
 *
 * 设计约束（实测踩坑后确定）：
 *   IShellLink::GetPath 解析「shell 命名空间快捷方式」（例如 UWP 的 LiveCaptions.lnk）
 *   必须在带消息泵的 STA 线程上跑。放到 QThreadPool 工作线程里会直接段错误。
 *   因此扫描拆成两步，全部在 GUI 线程执行，由 Controller 按时间片驱动：
 *
 *     buildTasks()  枚举阶段：目录遍历 + 读注册表，很快
 *     runTask()     处理阶段：解析 lnk + 提取图标，单个可能几十毫秒
 *
 * 当前覆盖：
 *   - 开始菜单（当前用户 + 所有用户）的 .lnk / .url
 *   - 注册表 Uninstall 项（仅取 DisplayIcon 指向真实 exe 的条目）
 * 待补（见 doc/2.方案设计.md §4）：UWP / 商店应用（FOLDERID_AppsFolder）
 */
class AppScanner
{
public:
    struct Options
    {
        bool startMenu = true;   // 开始菜单 .lnk / .url
        bool registry = true;    // 注册表 Uninstall
        bool uwp = false;        // 商店应用（未实现）
    };

    struct Task
    {
        enum Kind {
            Link,        // .lnk / .url，需要 COM 解析
            Executable,  // 已知 exe（多来自注册表 DisplayIcon），只需取图标
        };

        Kind kind = Link;
        QString path;    // 待解析/取图标的路径
        QString name;    // 显示名（注册表条目用；Link 走文件名）
        QString seed;    // 稳定 ID 种子
    };

    // 枚举阶段（GUI 线程，快）
    static QList<Task> buildTasks(const Options &opt);

    // 处理阶段（GUI 线程，单个任务可能耗时）
    static bool runTask(const Task &task, ScannedApp *out);

    // 由扫描结果构造条目
    static AppItem *toItem(const ScannedApp &data, QObject *parent);

    // 单个 .lnk / .exe / .url 路径 -> AppItem（拖入时复用）
    static AppItem *fromAnyPath(const QString &path, QObject *parent);
};
