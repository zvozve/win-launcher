#include "core/debugtrace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace {
bool g_enabled = false;

QString tracePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("winlauncher.trace"));
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
