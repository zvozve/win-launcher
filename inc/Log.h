// ======================================================================
//  Log.h  -  日志系统封装 (spdlog 薄封装)
//
//  为什么需要: 出问题时的现场只有"进程没了"这一条线索。控制台被
//  WIN32 子系统关掉了, 以前靠 --trace 写 winlauncher.trace, 但
//    · 只有手动加 --trace 才记, 而崩溃/自行退出往往发生在没加参数时;
//    · 每条日志 open/close 一次文件, 没有级别也没有时间戳;
//    · 没有崩溃现场(退出码/地址/栈)。
//  所以换成 spdlog: 常开、按天切分、分级、逐条 flush。
//
//  日志目录:
//    %LOCALAPPDATA%\WinLauncher\log\winlauncher-YYYY-MM-DD.log
//  ★ 为什么不放 exe 同级: dist 是 Enigma VB 单文件, 运行时 exe 目录是
//    临时解压容器, 写进去的日志随容器销毁一起消失 —— 最需要日志的时候
//    恰好没有。配置本来就写 LOCALAPPDATA, 日志跟着走才活得下来。
//
//  header-only 模式(SPDLOG_HEADER_ONLY 不需要额外定义, 只包含头文件),
//  第三方源码 vendored 在 third_party/spdlog, 不依赖 vcpkg。
// ======================================================================
#pragma once

#include <QString>

#include "spdlog/spdlog.h"

#include <string>

namespace wl {
namespace log {

// 初始化。必须在 QApplication 构造之后调用(之前拿不到 applicationDirPath)。
// 重复调用安全(只生效一次)。返回 false 表示目录不可写(降级为不写文件)。
bool init();

// 进程退出时调用: 刷盘并关闸。关闭之后所有日志出口变 no-op ——
// 退出阶段还会有对象析构写日志, 不断闸就会在已销毁的 logger 上崩
// (spdlog 的自由函数不检查 default_logger 是否为空)。
void shutdown();

QString logDir();
QString currentLogFile();

// spdlog 内部按 UTF-8 处理, 而 QString::toStdString() 走本地代码页
// (zh-CN 是 GBK), 直接传会把中文写成乱码且不可逆。统一走这里。
inline std::string utf8(const QString &s)
{
    return s.toUtf8().toStdString();
}

// 日志总闸。理由见 shutdown() 的注释。
bool active();

inline void logAt(spdlog::level::level_enum lvl, const std::string &msg)
{
    if (!active())
        return;
    // ★ 必须写成 "{}" 占位再传值, 不能 spdlog::log(lvl, msg):
    //   后者把 msg 当成 **format string**, 消息里只要有花括号
    //   (QML 报错、JSON 片段、用户输入都会带) 就会抛 spdlog_ex,
    //   在日志函数里抛异常 = 直接 terminate, 排查工具反成崩溃源。
    spdlog::log(lvl, "{}", msg);
}

inline void debug(const QString &msg)
{
    logAt(spdlog::level::debug, utf8(msg));
}

inline void info(const QString &msg)
{
    logAt(spdlog::level::info, utf8(msg));
}

inline void warn(const QString &msg)
{
    logAt(spdlog::level::warn, utf8(msg));
}

inline void error(const QString &msg)
{
    logAt(spdlog::level::err, utf8(msg));
}

inline void critical(const QString &msg)
{
    logAt(spdlog::level::critical, utf8(msg));
}

// 把当前调用栈写进日志。用于回答"到底是谁触发的退出/隐藏" ——
// 这类问题看代码看不出来, 但栈一看就知道。符号化需要 PDB(dbghelp),
// 找不到 PDB 时退化成 模块名+偏移, 仍然能定位到函数区间。
void stackTrace(const QString &tag, int skipFrames = 2, int maxFrames = 24);

} // namespace log
} // namespace wl
