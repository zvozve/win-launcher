#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/*
 * Everything 搜索接入（动态加载 Everything64.dll，不引入 SDK 头文件）。
 *
 * 设计目标：搜索栏可以按「搜索源」切换本地过滤 / Everything 全盘搜索，
 * 后续再按类型（exe / lnk / 文档）过滤结果。
 *
 * 现状：DLL 探测 + 调用链已实现，但本机没有 Everything 运行环境，
 *       未经实测；未就绪时 available() == false，UI 会自动回退到本地过滤。
 */
class EverythingSearch : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)

public:
    explicit EverythingSearch(QObject *parent = nullptr);
    ~EverythingSearch() override;

    bool isAvailable() const { return m_available; }

    // 返回全路径列表；不可用时返回空列表
    QStringList search(const QString &query, int limit = 100);

signals:
    void availabilityChanged();

private:
    void probe();

    bool m_available = false;
    bool m_probed = false;
    void *m_module = nullptr;

    // Everything SDK 函数指针
    void (*m_setSearchW)(const wchar_t *) = nullptr;
    void (*m_setRequestFlags)(unsigned int) = nullptr;
    void (*m_setMax)(unsigned int) = nullptr;
    int (*m_queryW)(int) = nullptr;
    unsigned int (*m_getNumResults)() = nullptr;
    void (*m_getResultFullPathNameW)(unsigned int, wchar_t *, unsigned int) = nullptr;
};
