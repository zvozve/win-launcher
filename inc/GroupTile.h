#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "AppItem.h"

/*
 * GroupTile —— 启动台网格里的一格：要么是一个应用，要么是一个「折叠起来的文件夹」。
 *
 * 为什么需要这一层：同一个来源装出来的一堆入口（本机 J-Link 一家 44 个图标）
 * 在网格里逐条平铺根本没法看。归组键来自开始菜单的目录结构（见 AppScanner），
 * 于是网格的 model 变成「应用 + 文件夹」的混合列表 —— 这种 map/reduce 放在 C++
 * 里做，QML 只负责画格子。
 *
 * 直接持有 AppItem*（QML 里读 item.name / item.iconUrl 和以前一样）：
 * GroupTile 归 Controller 所有，Controller 每次 rebuildVisible 整批重建，
 * 不会出现悬空指针。
 */
class GroupTile : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isFolder READ isFolder CONSTANT)
    Q_PROPERTY(AppItem *item READ item CONSTANT)             // 非文件夹时才是非空
    Q_PROPERTY(QString label READ label CONSTANT)            // 应用名 / 文件夹名
    Q_PROPERTY(QString groupKey READ groupKey CONSTANT)      // 文件夹的归组键（非文件夹为空）
    Q_PROPERTY(int count READ count CONSTANT)                // 文件夹里的条目数
    Q_PROPERTY(QList<QObject *> members READ members CONSTANT)
    // 文件夹预览用（最多 4 个）。QML 里要按下标取，所以是 QList<AppItem*> 而不是 URL 表 ——
    // 后者在 QML 里取下标要绕一圈，还拿不到别的属性。
    Q_PROPERTY(QList<QObject *> previewItems READ previewItems CONSTANT)

public:
    explicit GroupTile(AppItem *app, QObject *parent = nullptr);

    static GroupTile *makeFolder(const QString &groupKey,
                                 const QString &label,
                                 const QList<AppItem *> &members,
                                 QObject *parent = nullptr);

    bool isFolder() const { return !m_groupKey.isEmpty(); }
    AppItem *item() const { return m_app; }
    QString label() const { return m_label; }
    QString groupKey() const { return m_groupKey; }
    int count() const { return m_members.size(); }
    QList<QObject *> members() const;
    QList<QObject *> previewItems() const;

    // C++ 侧用：文件夹里的成员（非文件夹时是空表）
    const QList<AppItem *> &memberItems() const { return m_members; }

private:
    AppItem *m_app = nullptr;
    QString m_groupKey;
    QString m_label;
    QList<AppItem *> m_members;
};
