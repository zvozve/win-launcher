#include "GroupTile.h"

GroupTile::GroupTile(AppItem *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_label(app ? app->name() : QString())
{
}

GroupTile *GroupTile::makeFolder(const QString &groupKey,
                                const QString &label,
                                const QList<AppItem *> &members,
                                QObject *parent)
{
    auto *tile = new GroupTile(nullptr, parent);
    tile->m_groupKey = groupKey;
    tile->m_label = label;
    tile->m_members = members;
    return tile;
}

QList<QObject *> GroupTile::members() const
{
    QList<QObject *> out;
    out.reserve(m_members.size());
    for (AppItem *item : m_members)
        out.append(item);
    return out;
}

QList<QObject *> GroupTile::previewItems() const
{
    QList<QObject *> out;
    const int take = qMin(4, m_members.size());
    out.reserve(take);
    for (int i = 0; i < take; ++i)
        out.append(m_members.at(i));
    return out;
}
