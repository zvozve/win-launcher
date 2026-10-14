#include "core/debugtrace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace {
bool g_enabled = false;

QString tracePath()
{
    // --trace 在 QApplication 构造之前就可能开始打点，此时 applicationDirPath()
    // 还是空的（并会打印一条警告），退回当前工作目录。
    const QString dir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath()
                                                     : QDir::currentPath();
    return QDir(dir).filePath(QStringLiteral("winlauncher.trace"));
}
} // namespace

void wlTraceEnable(bool on)
{
    g_enabled = on;
}

bool wlTraceEnabled()
{
    return g_enabled;
}

void wlTrace(const char *message)
{
    if (!g_enabled)
        return;

    QFile f(tracePath());
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    f.write(message);
    f.write("\n");
    f.close();
}

void wlTrace(const QString &message)
{
    wlTrace(message.toUtf8().constData());
}
