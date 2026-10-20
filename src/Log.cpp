#include "Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#endif

namespace wl {
namespace log {

namespace {

std::atomic<bool> g_inited{false};
QString g_dir;

// 保留最近 30 天。daily_file_sink 的 max_files 是 uint16_t,
// 传 int 会窄化, 必须显式用 uint16_t。
constexpr std::uint16_t kMaxFiles = 30;

} // namespace

bool init()
{
    if (g_inited)
        return true;

    // 目录优先级: 用户数据目录 > exe 同级。
    // ★ dist 的单文件版必须走前者: Enigma VB 把 exe 目录虚拟化成临时
    //   容器, 在那里 mkpath 可能"成功"但文件随容器销毁而消失。
    // ★ init() 在 QApplication 构造**之前**就会被调用(为了记下构造期的
    //   警告), 那时 applicationDirPath() 是空的还会打警告, 所以要判空。
    QStringList candidates;
    const QString dataDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataDir.isEmpty())
        candidates << dataDir + QStringLiteral("/WinLauncher/log");
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/log");

    g_dir.clear();
    for (const QString &c : candidates) {
        if (c.isEmpty() || !QDir().mkpath(c))
            continue;
        // 真写一个探针再认 —— 虚拟化目录 mkpath 会假成功。
        const QString probe = c + QStringLiteral("/.wtest");
        QFile f(probe);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
            QFile::remove(probe);
            g_dir = c;
            break;
        }
    }
    if (g_dir.isEmpty()) {
        // 一个都写不了就别硬写, 否则每条日志都要报一次错。
        g_inited = true;
        return false;
    }

    // spdlog 内部按窄字符处理路径, 用 native separators 最稳。
    const std::string base =
            QDir::toNativeSeparators(g_dir + QStringLiteral("/winlauncher")).toStdString();

    try {
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_level(spdlog::level::warn);

        // ★ spdlog 1.14 的真实签名:
        //   (filename, hour, minute, truncate, max_files, handlers)
        //   第 4 个参数是 bool truncate, 不是 max_files —— 写错顺序会变成
        //   "每次启动清空当天日志", 恰好丢掉崩溃前的现场。
        auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                base, 23, 59, /*truncate=*/false, kMaxFiles);
        file->set_level(spdlog::level::trace);

        auto logger = std::make_shared<spdlog::logger>(
                "winlauncher", spdlog::sinks_init_list{console, file});
        logger->set_level(spdlog::level::trace);
        // 逐条 flush: 进程被强杀/崩溃时缓冲区里的东西全丢, 而那正是要看的部分。
        // 本应用日志量很小(一次启动几十行), 代价可以忽略。
        // 用 trace 而不是 info 作为阈值 —— warn 以上的"少见日志"反而不是
        // 最需要保住的, 崩溃前最后几行往往就是 info。
        logger->flush_on(spdlog::level::trace);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t%t] %v");

        spdlog::set_default_logger(std::move(logger));
        g_inited = true;
        return true;
    } catch (const spdlog::spdlog_ex &) {
        // 初始化失败时绝不能调 error() —— 默认 logger 还没建好, 会递归。
        g_inited = true;
        return false;
    }
}

bool active()
{
    return g_inited.load(std::memory_order_relaxed);
}

void shutdown()
{
    if (!g_inited)
        return;
    if (auto lg = spdlog::default_logger())
        lg->flush();
    // 先落闸再销毁: spdlog::shutdown() 之后 default logger 就没了,
    // 而调用方在这之后还要析构一堆会写日志的对象。
    g_inited = false;
    spdlog::shutdown();
}

QString logDir()
{
    return g_dir;
}

QString currentLogFile()
{
    try {
        auto lg = spdlog::get("winlauncher");
        for (auto &sink : lg->sinks()) {
            if (auto *daily = dynamic_cast<spdlog::sinks::daily_file_sink_mt *>(sink.get()))
                return QString::fromStdString(daily->filename());
        }
    } catch (const spdlog::spdlog_ex &) {
        // 拿不到就退回目录
    }
    return g_dir;
}

void stackTrace(const QString &tag, int skipFrames, int maxFrames)
{
    if (!active())
        return;

#ifdef Q_OS_WIN
    static std::once_flag once;
    static std::mutex symMutex;
    std::call_once(once, []() {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    });

    void *frames[64];
    if (maxFrames > 64)
        maxFrames = 64;
    const USHORT n = CaptureStackBackTrace(static_cast<DWORD>(skipFrames), maxFrames, frames, nullptr);

    QStringList lines;
    lines.reserve(n);
    {
        // DbgHelp 不是线程安全的, 全局串行化。
        std::lock_guard<std::mutex> lock(symMutex);
        HANDLE proc = GetCurrentProcess();
        char buf[sizeof(SYMBOL_INFO) + 512] = {};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 512;

        for (USHORT i = 0; i < n; ++i) {
            const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
            DWORD64 disp = 0;
            if (SymFromAddr(proc, addr, &disp, sym)) {
                IMAGEHLP_LINE64 line = {};
                line.SizeOfStruct = sizeof(line);
                DWORD ldisp = 0;
                if (SymGetLineFromAddr64(proc, addr, &ldisp, &line)) {
                    lines << QStringLiteral("#%1 %2 +0x%3  (%4:%5)")
                                     .arg(i)
                                     .arg(QString::fromLatin1(sym->Name))
                                     .arg(disp, 0, 16)
                                     .arg(QString::fromLocal8Bit(line.FileName))
                                     .arg(line.LineNumber);
                } else {
                    lines << QStringLiteral("#%1 %2 +0x%3")
                                     .arg(i)
                                     .arg(QString::fromLatin1(sym->Name))
                                     .arg(disp, 0, 16);
                }
            } else {
                // 没有 PDB 时退化: 模块名 + 偏移, 仍能定位到函数区间
                HMODULE mod = nullptr;
                QString modName = QStringLiteral("?");
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(frames[i]), &mod)) {
                    wchar_t path[MAX_PATH] = {};
                    GetModuleFileNameW(mod, path, MAX_PATH);
                    modName = QFileInfo(QString::fromWCharArray(path)).fileName();
                }
                lines << QStringLiteral("#%1 %2+0x%3")
                                 .arg(i)
                                 .arg(modName)
                                 .arg(addr - reinterpret_cast<DWORD64>(mod), 0, 16);
            }
        }
    }
    info(QStringLiteral("%1\n%2").arg(tag, lines.join(QLatin1Char('\n'))));
#else
    Q_UNUSED(tag)
    Q_UNUSED(skipFrames)
    Q_UNUSED(maxFrames)
#endif
}

} // namespace log
} // namespace wl
