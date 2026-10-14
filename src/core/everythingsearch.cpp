#include "core/everythingsearch.h"

#include <QDebug>

#include <algorithm>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// Everything SDK 请求标志
constexpr unsigned int kRequestFullPathAndFileName = 0x00000004;

} // namespace

EverythingSearch::EverythingSearch(QObject *parent)
    : QObject(parent)
{
    probe();
}

EverythingSearch::~EverythingSearch()
{
    if (m_module)
        FreeLibrary(static_cast<HMODULE>(m_module));
}

void EverythingSearch::probe()
{
    if (m_probed)
        return;
    m_probed = true;

    HMODULE mod = LoadLibraryW(L"Everything64.dll");
    if (!mod)
        mod = LoadLibraryW(L"Everything.dll");
    if (!mod) {
        qInfo("EverythingSearch: 未找到 Everything64.dll，搜索源回退为本地过滤");
        return;
    }

    m_setSearchW = reinterpret_cast<void (*)(const wchar_t *)>(
        GetProcAddress(mod, "Everything_SetSearchW"));
    m_setRequestFlags = reinterpret_cast<void (*)(unsigned int)>(
        GetProcAddress(mod, "Everything_SetRequestFlags"));
    m_setMax = reinterpret_cast<void (*)(unsigned int)>(
        GetProcAddress(mod, "Everything_SetMax"));
    m_queryW = reinterpret_cast<int (*)(int)>(GetProcAddress(mod, "Everything_QueryW"));
    m_getNumResults = reinterpret_cast<unsigned int (*)()>(
        GetProcAddress(mod, "Everything_GetNumResults"));
    m_getResultFullPathNameW = reinterpret_cast<void (*)(unsigned int, wchar_t *, unsigned int)>(
        GetProcAddress(mod, "Everything_GetResultFullPathNameW"));

    const bool complete = m_setSearchW && m_queryW && m_getNumResults && m_getResultFullPathNameW;
    if (!complete) {
        qWarning("EverythingSearch: Everything64.dll 存在但导出符号不完整，放弃接入");
        FreeLibrary(mod);
        return;
    }

    m_module = mod;
    m_available = true;
    emit availabilityChanged();
    qInfo("EverythingSearch: Everything64.dll 就绪");
}

QStringList EverythingSearch::search(const QString &query, int limit)
{
    QStringList result;
    if (!m_available || query.trimmed().isEmpty())
        return result;

    m_setSearchW(reinterpret_cast<const wchar_t *>(query.utf16()));
    if (m_setRequestFlags)
        m_setRequestFlags(kRequestFullPathAndFileName);
    if (m_setMax)
        m_setMax(static_cast<unsigned int>(limit));

    if (!m_queryW(TRUE)) // TRUE = 同步等待
        return result;

    const unsigned int count = m_getNumResults();
    const unsigned int capped = qMin<unsigned int>(count, static_cast<unsigned int>(limit));

    std::wstring buffer(32768, L'\0');
    for (unsigned int i = 0; i < capped; ++i) {
        std::fill(buffer.begin(), buffer.end(), L'\0');
        m_getResultFullPathNameW(i, buffer.data(), static_cast<unsigned int>(buffer.size()));
        const QString path = QString::fromWCharArray(buffer.c_str());
        if (!path.isEmpty())
            result.append(path);
    }
    return result;
}
