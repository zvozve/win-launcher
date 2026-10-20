#include "CrashHandler.h"

#include "Log.h"

#include <QDir>
#include <QStandardPaths>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

namespace wl {
namespace crash {

namespace {

// ── 崩溃期禁止分配堆、禁止 QString、禁止锁。路径用固定宽字符缓冲。 ──
wchar_t g_dir[MAX_PATH] = {};
wchar_t g_logFile[MAX_PATH] = {};

using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                           PMINIDUMP_EXCEPTION_INFORMATION,
                                           PMINIDUMP_USER_STREAM_INFORMATION,
                                           PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpWriteDumpFn g_miniDump = nullptr;

// 防重入: 崩溃处理器里再崩会递归, 只让第一个进来。
volatile LONG g_inReport = 0;

struct EnterGuard
{
    bool ok;
    EnterGuard() { ok = (InterlockedCompareExchange(&g_inReport, 1, 0) == 0); }
    ~EnterGuard()
    {
        if (ok)
            InterlockedExchange(&g_inReport, 0);
    }
};

void writeAll(HANDLE h, const void *data, unsigned len)
{
    if (h == INVALID_HANDLE_VALUE || !data || len == 0)
        return;
    const char *p = static_cast<const char *>(data);
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, len, &written, nullptr) || written == 0)
            return;
        p += written;
        len -= written;
    }
}

void moduleOf(const void *addr, wchar_t *out, unsigned outChars, unsigned long long *offset)
{
    HMODULE mod = nullptr;
    if (offset)
        *offset = 0;
    if (!out || outChars == 0)
        return;
    out[0] = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(addr), &mod)
        && mod) {
        wchar_t full[MAX_PATH] = {};
        GetModuleFileNameW(mod, full, MAX_PATH);
        const wchar_t *name = full;
        for (const wchar_t *p = full; *p; ++p) {
            if (*p == L'\\' || *p == L'/')
                name = p + 1;
        }
        wcsncpy(out, name, outChars - 1);
        out[outChars - 1] = 0;
        if (offset)
            *offset = reinterpret_cast<unsigned long long>(addr)
                      - reinterpret_cast<unsigned long long>(mod);
    } else {
        wcsncpy(out, L"?", outChars - 1);
    }
}

void writeDump(EXCEPTION_POINTERS *ep, const wchar_t *path)
{
    if (!g_miniDump)
        return;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f, type,
               ep ? &info : nullptr, nullptr, nullptr);
    CloseHandle(f);
}

/**
 * 写一份纯文本现场报告 + 一个 minidump。
 *
 * @param kind  "unhandled-exception" / "terminate" / "signal-abort" ...
 * @param code  异常码或信号值(没有就填 0)
 * @param ep    SEH 现场(信号/terminate 路径为 nullptr)
 */
void report(const wchar_t *kind, unsigned long code, EXCEPTION_POINTERS *ep)
{
    EnterGuard guard;
    if (!guard.ok)
        return;
    if (g_dir[0] == 0)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t stamp[32] = {};
    swprintf(stamp, 32, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    wchar_t path[MAX_PATH] = {};
    swprintf(path, MAX_PATH, L"%s\\crash-%s-%lu.txt", g_dir, stamp,
             static_cast<unsigned long>(GetCurrentProcessId()));

    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;

    char line[1024] = {};
    int n = 0;

    n = snprintf(line, sizeof(line),
                 "[CRASH] kind=%ls\n"
                 "time=%04u-%02u-%02u %02u:%02u:%02u.%03u\n"
                 "pid=%lu tid=%lu\n",
                 kind, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds, static_cast<unsigned long>(GetCurrentProcessId()),
                 static_cast<unsigned long>(GetCurrentThreadId()));
    writeAll(f, line, static_cast<unsigned>(n));

    if (ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD *er = ep->ExceptionRecord;
        wchar_t mod[MAX_PATH] = {};
        unsigned long long off = 0;
        moduleOf(reinterpret_cast<const void *>(er->ExceptionAddress), mod,
                 static_cast<unsigned>(MAX_PATH), &off);

        n = snprintf(line, sizeof(line),
                     "exception=0x%08lX  address=0x%p  module=%ls+0x%llX  flags=0x%lX\n",
                     static_cast<unsigned long>(er->ExceptionCode), er->ExceptionAddress,
                     mod, off, static_cast<unsigned long>(er->ExceptionFlags));
        writeAll(f, line, static_cast<unsigned>(n));

        // 访问违例要区分读/写/执行, 这是判断"空指针写"还是"野指针读"的关键
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && er->NumberParameters >= 2) {
            const unsigned long long op = er->ExceptionInformation[0];
            const unsigned long long addr = er->ExceptionInformation[1];
            const char *opName = (op == 0) ? "read" : (op == 1) ? "write" : "execute";
            n = snprintf(line, sizeof(line), "access=%s at 0x%llX\n", opName, addr);
            writeAll(f, line, static_cast<unsigned>(n));
        }
    } else {
        n = snprintf(line, sizeof(line), "code=0x%08lX (no exception record)\n",
                     static_cast<unsigned long>(code));
        writeAll(f, line, static_cast<unsigned>(n));
    }

    // 日志路径: 崩溃前的运行轨迹都在那
    if (g_logFile[0]) {
        n = snprintf(line, sizeof(line), "log=%ls\n", g_logFile);
        writeAll(f, line, static_cast<unsigned>(n));
    }

    // 栈: 只写地址。符号化留给事后(有 PDB 时用 WinDbg/VS 打开 dmp 更完整)
    void *frames[48] = {};
    const USHORT count = CaptureStackBackTrace(1, 48, frames, nullptr);
    n = snprintf(line, sizeof(line), "backtrace (%u frames):\n", static_cast<unsigned>(count));
    writeAll(f, line, static_cast<unsigned>(n));
    for (USHORT i = 0; i < count; ++i) {
        wchar_t mod[MAX_PATH] = {};
        unsigned long long off = 0;
        moduleOf(frames[i], mod, static_cast<unsigned>(MAX_PATH), &off);
        n = snprintf(line, sizeof(line), "  #%u 0x%p  %ls+0x%llX\n",
                     static_cast<unsigned>(i), frames[i], mod, off);
        writeAll(f, line, static_cast<unsigned>(n));
    }

    CloseHandle(f);

    // minidump
    wchar_t dumpPath[MAX_PATH] = {};
    swprintf(dumpPath, MAX_PATH, L"%s\\crash-%s-%lu.dmp", g_dir, stamp,
             static_cast<unsigned long>(GetCurrentProcessId()));
    writeDump(ep, dumpPath);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *ep)
{
    report(L"unhandled-exception",
           ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0, ep);
    // 继续交给系统(WER) —— 保留系统级转储, 别把现场吞掉
    return EXCEPTION_CONTINUE_SEARCH;
}

void onTerminate()
{
    report(L"std::terminate", 0, nullptr);
    // terminate 之后进程本来就要死, 这里给系统留个 abort 记录
}

void onSignal(int sig)
{
    const wchar_t *kind = L"signal";
    switch (sig) {
    case SIGABRT:
        kind = L"signal-abort";
        break;
    case SIGSEGV:
        kind = L"signal-segv";
        break;
    case SIGFPE:
        kind = L"signal-fpe";
        break;
    case SIGILL:
        kind = L"signal-ill";
        break;
    case SIGINT:
        kind = L"signal-int";
        break;
    case SIGTERM:
        kind = L"signal-term";
        break;
    default:
        break;
    }
    report(kind, static_cast<unsigned long>(sig), nullptr);
    // 恢复默认处理并重新抛出, 让系统按正常流程终止(否则是未定义行为)
    signal(sig, SIG_DFL);
    raise(sig);
}

void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t)
{
    report(L"invalid-parameter", 0, nullptr);
}

void onPureCall()
{
    report(L"pure-virtual-call", 0, nullptr);
}

} // namespace

QString crashDir()
{
    return QString::fromWCharArray(g_dir);
}

void install()
{
    // 目录: 跟日志同处(LOCALAPPDATA\WinLauncher\log)。
    // 拿不到日志目录时退到临时目录, 至少别什么都不留。
    QString dir = wl::log::logDir();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
              + QStringLiteral("/WinLauncher");
        QDir().mkpath(dir);
    }
    const QString native = QDir::toNativeSeparators(dir);
    wcsncpy(g_dir, reinterpret_cast<const wchar_t *>(native.utf16()), MAX_PATH - 1);
    g_dir[MAX_PATH - 1] = 0;

    const QString logFile = QDir::toNativeSeparators(wl::log::currentLogFile());
    if (!logFile.isEmpty()) {
        wcsncpy(g_logFile, reinterpret_cast<const wchar_t *>(logFile.utf16()), MAX_PATH - 1);
        g_logFile[MAX_PATH - 1] = 0;
    }

    // 预先加载 dbghelp 并取到 MiniDumpWriteDump —— 崩溃现场再去 LoadLibrary
    // 有可能失败(加载器锁/内存不足), 那 dump 就没了。
    if (HMODULE dh = LoadLibraryW(L"dbghelp.dll")) {
        g_miniDump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<void *>(GetProcAddress(dh, "MiniDumpWriteDump")));
    }

    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_purecall_handler(onPureCall);

    signal(SIGABRT, onSignal);
    signal(SIGSEGV, onSignal);
    signal(SIGFPE, onSignal);
    signal(SIGILL, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
}

} // namespace crash
} // namespace wl
