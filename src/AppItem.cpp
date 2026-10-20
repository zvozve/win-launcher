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

/*
 * 归组键的最后一段就是文件夹名（"SEGGER\J-Link V7.98b" -> "J-Link V7.98b"）。
 * 放在 C++ 里算，QML 的文件夹瓦片上直接绑 groupLabel 就行。
 *
 * 末段是纯技术目录名时往上一层取：本机的火绒是
 * "火绒安全实验室\Sysdiag"，直接取末段会得到一个叫「Sysdiag」的文件夹，
 * 用户根本认不出那是什么。同理还有 bin / tools / x64 这类。
 */
QString AppItem::groupLabel() const
{
    if (m_group.isEmpty())
        return QString();
    QStringList parts = m_group.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QString();

    static const QStringList kGeneric = {
        QStringLiteral("sysdiag"), QStringLiteral("bin"),
        QStringLiteral("tools"),   QStringLiteral("tool"),
        QStringLiteral("utils"),   QStringLiteral("utilities"),
        QStringLiteral("app"),     QStringLiteral("apps"),
        QStringLiteral("application"), QStringLiteral("applications"),
        QStringLiteral("x86"),     QStringLiteral("x64"),
        QStringLiteral("win32"),   QStringLiteral("win64"),
        QStringLiteral("amd64"),
    };
    while (parts.size() > 1 && kGeneric.contains(parts.last().toLower()))
        parts.removeLast();
    return parts.last();
}

void AppItem::setGroup(const QString &v)
{
    if (m_group == v)
        return;
    m_group = v;
    emit groupChanged();
}

void AppItem::setRemovable(bool v)
{
    if (m_removable == v)
        return;
    m_removable = v;
    emit removableChanged();
}

void AppItem::setSizeBytes(qint64 v)
{
    if (m_sizeBytes == v)
        return;
    m_sizeBytes = v;
    emit sizeBytesChanged();
}

/*
 * 人类可读的大小。全盘搜索的详情列表要它；-1（未知/目录）显示成空，
 * 免得在「大小」列里塞个「0 B」误导人。
 */
QString AppItem::sizeText() const
{
    if (m_sizeBytes < 0)
        return QString();
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(m_sizeBytes);
    int unit = 0;
    while (v >= 1024.0 && unit < 4) {
        v /= 1024.0;
        ++unit;
    }
    // 字节数不带小数，其余保留一位（和资源管理器观感接近）
    return unit == 0 ? QStringLiteral("%1 B").arg(static_cast<qint64>(v))
                     : QStringLiteral("%1 %2").arg(QString::number(v, 'f', 1), QString::fromLatin1(units[unit]));
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
