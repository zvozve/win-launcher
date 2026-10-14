#include "EverythingSearch.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <algorithm>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// Everything SDK 请求标志（Everything.h）
constexpr unsigned int kRequestFullPathAndFileName = 0x00000004;

// 单条结果路径的缓冲（路径上限足够）
constexpr unsigned int kPathBufferChars = 32768;

} // namespace

EverythingSearch::EverythingSearch(QObject *parent)
    : QObject(parent)
{
    probe();
}

EverythingSearch::~EverythingSearch()
{
    if (m_module) {
        if (m_cleanUp)
            m_cleanUp();
        FreeLibrary(static_cast<HMODULE>(m_module));
    }
}

void EverythingSearch::probe()
{
    if (m_probed)
        return;
    m_probed = true;

    // Everything 本体安装目录里没有 SDK DLL，所以除了 exe 同目录也探一下默认安装位置。
    const QStringList candidates = {
        QStringLiteral("Everything64.dll"), // exe 目录 / PATH
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Everything64.dll")),
        QStringLiteral("C:/Program Files/Everything/Everything64.dll"),
        QStringLiteral("C:/Program Files (x86)/Everything/Everything64.dll"),
        QStringLiteral("Everything.dll"), // 32 位兜底
    };

    HMODULE mod = nullptr;
    for (const QString &candidate : candidates) {
        mod = LoadLibraryW(reinterpret_cast<const wchar_t *>(candidate.utf16()));
        if (mod) {
            m_loadedPath = candidate;
            break;
        }
    }

    if (!mod) {
        qInfo("EverythingSearch: 未找到 Everything64.dll，搜索源回退为本地过滤"
              "（可运行 scripts\\fetch-everything-sdk.bat 取回）");
        return;
    }

    m_setSearchW = reinterpret_cast<void (*)(const wchar_t *)>(
        GetProcAddress(mod, "Everything_SetSearchW"));
    m_setRequestFlags = reinterpret_cast<void (*)(unsigned int)>(
        GetProcAddress(mod, "Everything_SetRequestFlags"));
    m_setMax = reinterpret_cast<void (*)(unsigned int)>(GetProcAddress(mod, "Everything_SetMax"));
    m_setMatchCase = reinterpret_cast<void (*)(int)>(GetProcAddress(mod, "Everything_SetMatchCase"));
    m_setMatchPath = reinterpret_cast<void (*)(int)>(GetProcAddress(mod, "Everything_SetMatchPath"));
    m_setRegex = reinterpret_cast<void (*)(int)>(GetProcAddress(mod, "Everything_SetRegex"));
    m_queryW = reinterpret_cast<int (*)(int)>(GetProcAddress(mod, "Everything_QueryW"));
    m_getNumResults = reinterpret_cast<unsigned int (*)()>(
        GetProcAddress(mod, "Everything_GetNumResults"));
    m_getResultFullPathNameW = reinterpret_cast<void (*)(unsigned int, wchar_t *, unsigned int)>(
        GetProcAddress(mod, "Everything_GetResultFullPathNameW"));
    m_cleanUp = reinterpret_cast<void (*)()>(GetProcAddress(mod, "Everything_CleanUp"));

    const bool complete = m_setSearchW && m_queryW && m_getNumResults && m_getResultFullPathNameW;
    if (!complete) {
        qWarning("EverythingSearch: %s 存在但导出符号不完整，放弃接入", qPrintable(m_loadedPath));
        FreeLibrary(mod);
        m_loadedPath.clear();
        return;
    }

    m_module = mod;
    m_available = true;
    emit availabilityChanged();
    qInfo("EverythingSearch: 已加载 %s", qPrintable(m_loadedPath));
}

QStringList EverythingSearch::search(const QString &query, int limit, const QString &extFilter)
{
    QStringList result;
    const QString needle = query.trimmed();
    if (!m_available || needle.isEmpty())
        return result;

    QString expression = needle;
    if (!extFilter.isEmpty())
        expression += QStringLiteral(" ext:") + extFilter;

    m_setSearchW(reinterpret_cast<const wchar_t *>(expression.utf16()));
    if (m_setMatchCase)
        m_setMatchCase(FALSE); // 大小写不敏感
    if (m_setMatchPath)
        m_setMatchPath(FALSE); // 默认只比文件名，不比路径
    if (m_setRegex)
        m_setRegex(FALSE);
    if (m_setRequestFlags)
        m_setRequestFlags(kRequestFullPathAndFileName);
    if (m_setMax)
        m_setMax(static_cast<unsigned int>(limit));

    if (!m_queryW(TRUE)) // TRUE = 同步等待（内部会泵消息）
        return result;

    const unsigned int count = m_getNumResults();
    const unsigned int capped = qMin<unsigned int>(count, static_cast<unsigned int>(limit));

    std::wstring buffer(kPathBufferChars, L'\0');
    for (unsigned int i = 0; i < capped; ++i) {
        std::fill(buffer.begin(), buffer.end(), L'\0');
        m_getResultFullPathNameW(i, buffer.data(), kPathBufferChars);
        const QString path = QString::fromWCharArray(buffer.c_str());
        if (!path.isEmpty())
            result.append(path);
    }
    return result;
}
