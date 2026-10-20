#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/*
 * 一条全盘命中。
 *
 * 大小/扩展名都来自 Everything **索引**，不访问磁盘 —— 这是关键：
 * 索引里可能有已断开/离线的卷，对命中做 QFileInfo::stat 会等超时把 GUI 冻住
 * （「搜 1.4 pdf 卡死」就是这么来的）。所以这里一次查询把展示需要的东西全取回来。
 */
struct EverythingHit
{
    QString fullPath;
    QString fileName;   // 由 fullPath 纯字符串拆分，不访问磁盘
    QString dirPath;    // 同上
    QString extension;  // 小写、不带点
    qint64 size = -1;   // 字节；-1 = 未知（目录 / 未索引）
    bool isFolder = false;
};

/*
 * Everything 搜索接入（动态加载 Everything64.dll，不引 SDK 头文件/静态库）。
 *
 * 用途：搜索栏「全盘」档按名称查整个 NTFS 索引，是「本地」过滤（只管已扫到的
 * 条目）的补充。
 *
 * DLL 查找顺序见 probe()：exe 同目录 → C:\Program Files\Everything\ → Program Files (x86)。
 * 注意 Everything 本体安装目录里**没有**这个 DLL，要用 scripts\fetch-everything-sdk.bat
 * 从 voidtools 的 Everything-SDK.zip 取，构建时由 build.bat 拷到 exe 旁。
 *
 * 未就绪时 available() == false，UI 自动回退本地过滤。
 */
class EverythingSearch : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(QString loadedPath READ loadedPath NOTIFY availabilityChanged)

public:
    explicit EverythingSearch(QObject *parent = nullptr);
    ~EverythingSearch() override;

    bool isAvailable() const { return m_available; }
    QString loadedPath() const { return m_loadedPath; }

    /*
     * 返回命中（含大小/扩展名）；不可用或查询为空时返回空列表。
     * extFilter 形如 "exe;lnk;url"，非空时按 Everything 的 ext: 语法拼进查询，
     * 让索引侧先做类型过滤；留空则全部返回，由调用方自己筛。
     * 注意 Everything_QueryW(TRUE) 是阻塞调用（内部泵消息），别放高频路径。
     */
    QList<EverythingHit> search(const QString &query, int limit = 100,
                                const QString &extFilter = QString());

signals:
    void availabilityChanged();

private:
    void probe();

    bool m_available = false;
    bool m_probed = false;
    void *m_module = nullptr;
    QString m_loadedPath;

    // Everything SDK 函数指针
    void (*m_setSearchW)(const wchar_t *) = nullptr;
    void (*m_setRequestFlags)(unsigned int) = nullptr;
    void (*m_setMax)(unsigned int) = nullptr;
    void (*m_setMatchCase)(int) = nullptr;
    void (*m_setMatchPath)(int) = nullptr;
    void (*m_setRegex)(int) = nullptr;
    int (*m_queryW)(int) = nullptr;
    unsigned int (*m_getNumResults)() = nullptr;
    void (*m_getResultFullPathNameW)(unsigned int, wchar_t *, unsigned int) = nullptr;
    int (*m_getResultSize)(unsigned int, void *) = nullptr;  // LARGE_INTEGER*
    int (*m_isFolderResult)(unsigned int) = nullptr;
    void (*m_cleanUp)() = nullptr;
};
