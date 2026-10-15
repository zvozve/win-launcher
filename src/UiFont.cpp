#include "UiFont.h"

#include <QFontDatabase>

namespace {

/*
 * 挑一个可用的 Fluent 图标字体 family。
 *
 * Win11 是 "Segoe Fluent Icons"；Win10 1809 以前是 "Segoe MDL2 Assets"，
 * 码点大部分相同 —— 两个都没有就返回空串，QML 会退回纯文字菜单。
 */
QString pickIconFamily()
{
    const QStringList families = QFontDatabase::families();
    const QStringList wanted = {
        QStringLiteral("Segoe Fluent Icons"),
        QStringLiteral("Segoe MDL2 Assets"),
    };
    for (const QString &name : wanted) {
        if (families.contains(name, Qt::CaseInsensitive))
            return name;
    }
    return QString();
}

} // namespace

UiFont::UiFont(QObject *parent)
    : QObject(parent)
    , m_family(pickIconFamily())
{
}
