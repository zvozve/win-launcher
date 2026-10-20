#include "DebugTrace.h"

#include "Log.h"

#include <QString>

namespace {
// --trace 的语义保留: 打开时把 debug 级别也往 stderr 打(开发期看控制台)。
// 但无论开不开, 文件里都记 —— 见 Log.h 的说明。
bool g_trace = false;
} // namespace

void wlTraceEnable(bool on)
{
    g_trace = on;
}

bool wlTraceEnabled()
{
    return g_trace;
}

void wlTrace(const char *message)
{
    if (!message || !*message)
        return;
    if (!wl::log::active())
        return;
    // 中文消息在源码里是 UTF-8 字面量; 走 toUtf8 路径保持与 QString 重载
    // 一致, 避免 GBK/UTF-8 混写导致日志乱码。
    wl::log::logAt(spdlog::level::info, std::string(message));
}

void wlTrace(const QString &message)
{
    wl::log::info(message);
}
