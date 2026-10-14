#include "AppItem.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

const QString AppItem::KindDesktop = QStringLiteral("desktop");
const QString AppItem::KindUrl = QStringLiteral("url");
const QString AppItem::KindUwp = QStringLiteral("uwp");
const QString AppItem::KindManual = QStringLiteral("manual");

AppItem::AppItem(QObject *parent)
    : QObject(parent)
    , m_kind(KindDesktop)
{
}

QUrl AppItem::iconUrl() const
{
    if (m_iconPath.isEmpty())
        return QUrl();
    return QUrl::fromLocalFile(m_iconPath);
}

void AppItem::setName(const QString &v)
{
    if (m_name == v)
        return;
    m_name = v;
    emit nameChanged();
}

void AppItem::setKind(const QString &v)
{
    if (m_kind == v)
        return;
    m_kind = v;
    emit kindChanged();
}

void AppItem::setTarget(const QString &v)
{
    if (m_target == v)
        return;
    m_target = v;
    emit targetChanged();
}

void AppItem::setArguments(const QString &v)
{
    if (m_arguments == v)
        return;
    m_arguments = v;
    emit argumentsChanged();
}

void AppItem::setWorkingDir(const QString &v)
{
    if (m_workingDir == v)
        return;
    m_workingDir = v;
    emit workingDirChanged();
}

void AppItem::setAumid(const QString &v)
{
    if (m_aumid == v)
        return;
    m_aumid = v;
    emit aumidChanged();
}

void AppItem::setSourcePath(const QString &v)
{
    if (m_sourcePath == v)
        return;
    m_sourcePath = v;
    emit sourcePathChanged();
}

void AppItem::setIconPath(const QString &v)
{
    if (m_iconPath == v)
        return;
    m_iconPath = v;
    emit iconPathChanged();
}

void AppItem::setCategory(const QString &v)
{
    if (m_category == v)
        return;
    m_category = v;
    emit categoryChanged();
}

void AppItem::setRemovable(bool v)
{
    if (m_removable == v)
        return;
    m_removable = v;
    emit removableChanged();
}

void AppItem::rebindId(const QString &seed)
{
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).toHex();
    m_id = QString::fromLatin1(hash.left(16));
}

QJsonObject AppItem::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), m_id);
    o.insert(QStringLiteral("name"), m_name);
    o.insert(QStringLiteral("kind"), m_kind);
    o.insert(QStringLiteral("target"), m_target);
    o.insert(QStringLiteral("args"), m_arguments);
    o.insert(QStringLiteral("workingDir"), m_workingDir);
    o.insert(QStringLiteral("aumid"), m_aumid);
    o.insert(QStringLiteral("source"), m_sourcePath);
    o.insert(QStringLiteral("icon"), m_iconPath);
    o.insert(QStringLiteral("category"), m_category);
    o.insert(QStringLiteral("removable"), m_removable);
    return o;
}

AppItem *AppItem::fromJson(const QJsonObject &obj, QObject *parent)
{
    auto *item = new AppItem(parent);
    item->m_id = obj.value(QStringLiteral("id")).toString();
    item->m_name = obj.value(QStringLiteral("name")).toString();
    item->m_kind = obj.value(QStringLiteral("kind")).toString(AppItem::KindDesktop);
    item->m_target = obj.value(QStringLiteral("target")).toString();
    item->m_arguments = obj.value(QStringLiteral("args")).toString();
    item->m_workingDir = obj.value(QStringLiteral("workingDir")).toString();
    item->m_aumid = obj.value(QStringLiteral("aumid")).toString();
    item->m_sourcePath = obj.value(QStringLiteral("source")).toString();
    item->m_iconPath = obj.value(QStringLiteral("icon")).toString();
    item->m_category = obj.value(QStringLiteral("category")).toString();
    item->m_removable = obj.value(QStringLiteral("removable")).toBool(false);
    return item;
}
