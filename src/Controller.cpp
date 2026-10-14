#include "Controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <utility>

#include "AppItem.h"
#include "DebugTrace.h"
#include "HotkeyManager.h"
#include "IconProvider.h"
#include "Launcher.h"
#include "LauncherWindow.h"

namespace {

// 每片扫描占用主线程的时间预算：太小会频繁让出事件循环，
// 太大会让面板卡顿。25ms 是「跟手」与「扫得快」的折中。
constexpr int kScanSliceMs = 25;

bool g_autoStartLaunch = false;

QString layoutPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("layout.json"));
}

} // namespace

Controller::Controller(Config *config, EverythingSearch *everything, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_everything(everything)
{
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(140);
    connect(m_searchDebounce, &QTimer::timeout, this, &Controller::rebuildVisible);

    loadManualApps();
    rebuildVisible();

    setStatus(QStringLiteral("就绪"));
}

// ---- 初始化 ---------------------------------------------------------------

void Controller::attachWindow(LauncherWindow *window)
{
    m_window = window;
    if (!m_window)
        return;

    m_window->setHideOnFocusOut(m_config->hideOnFocusOut());
    m_window->setDarkBackdrop(m_config->darkMode());
    connect(m_config, &Config::hideOnFocusOutChanged, m_window,
            [this] { m_window->setHideOnFocusOut(m_config->hideOnFocusOut()); });
    connect(m_config, &Config::darkModeChanged, m_window,
            [this] { m_window->setDarkBackdrop(m_config->darkMode()); });
}

void Controller::attachHotkeys(HotkeyManager *hotkeys)
{
    m_hotkeys = hotkeys;
    if (!m_hotkeys)
        return;
    connect(m_hotkeys, &HotkeyManager::activated, this, &Controller::onHotkeyActivated);
}

void Controller::onHotkeyActivated()
{
    togglePanel();
}

void Controller::togglePanel()
{
    if (!m_window)
        return;
    m_window->toggleAtCursor();
}

// ---- 扫描（GUI 线程分片）--------------------------------------------------

void Controller::rescan()
{
    if (m_scanning)
        return;

    m_scanning = true;
    emit scanningChanged();
    setError(QString());

    AppScanner::Options opt;
    opt.startMenu = true;
    opt.registry = m_config->includeRegistry();
    opt.uwp = false; // TODO: 商店应用枚举

    m_scanTasks = AppScanner::buildTasks(opt);
    m_scanBuffer.clear();
    m_scanBuffer.reserve(m_scanTasks.size());
    m_scanIndex = 0;

    emit scanProgressChanged();

    if (m_scanTasks.isEmpty()) {
        finishScan();
        return;
    }

    setStatus(QStringLiteral("正在扫描 0 / %1…").arg(m_scanTasks.size()));
    QTimer::singleShot(0, this, &Controller::processScanChunk);
}

void Controller::processScanChunk()
{
    QElapsedTimer slice;
    slice.start();

    int lastReported = -1;
    while (m_scanIndex < m_scanTasks.size() && slice.elapsed() < kScanSliceMs) {
        if (m_scanIndex < 3)
            wlTrace(QStringLiteral("task #%1: %2").arg(m_scanIndex).arg(m_scanTasks.at(m_scanIndex).path));

        ScannedApp app;
        if (AppScanner::runTask(m_scanTasks.at(m_scanIndex), &app))
            m_scanBuffer.append(app);
        ++m_scanIndex;

        // 每 20 个（且至少隔一片）刷一次进度，避免刷爆信号
        if (m_scanIndex % 20 == 0 && lastReported != m_scanIndex) {
            lastReported = m_scanIndex;
            setStatus(QStringLiteral("正在扫描 %1 / %2…").arg(m_scanIndex).arg(m_scanTasks.size()));
            emit scanProgressChanged();
        }
    }

    emit scanProgressChanged();

    if (m_scanIndex < m_scanTasks.size()) {
        QTimer::singleShot(0, this, &Controller::processScanChunk);
        return;
    }
    finishScan();
}

void Controller::finishScan()
{
    // 去重：开始菜单条目排在前面，注册表条目只补它没覆盖到的目标
    QSet<QString> seenTargets;
    QList<ScannedApp> unique;
    unique.reserve(m_scanBuffer.size());
    for (const ScannedApp &app : std::as_const(m_scanBuffer)) {
        const QString key = app.target.isEmpty() ? app.seed.toLower() : app.target.toLower();
        if (key.isEmpty() || seenTargets.contains(key))
            continue;
        seenTargets.insert(key);
        unique.append(app);
    }

    qDeleteAll(m_all);
    m_all.clear();
    m_all.reserve(unique.size());
    for (const ScannedApp &app : std::as_const(unique))
        m_all.append(AppScanner::toItem(app, this));

    m_scanBuffer.clear();
    m_scanTasks.clear();
    m_scanIndex = 0;

    m_scanning = false;
    emit scanningChanged();
    emit scanProgressChanged();

    rebuildVisible();
    setStatus(QStringLiteral("共 %1 个应用").arg(m_all.size()));
    wlTrace(QStringLiteral("finishScan: %1 items").arg(m_all.size()));
}

void Controller::clearCacheAndRescan()
{
    IconProvider::clearCache();
    rescan();
}

// ---- 列表构建 -------------------------------------------------------------

void Controller::rebuildVisible()
{
    // 1) 合并：手动条目优先，扫描结果按 id 去重
    QList<QObject *> merged;
    QSet<QString> ids;

    for (AppItem *item : std::as_const(m_manual)) {
        ids.insert(item->id());
        merged.append(item);
    }
    for (AppItem *item : std::as_const(m_all)) {
        if (ids.contains(item->id()))
            continue; // 与手动条目重合，保留手动那份
        ids.insert(item->id());
        merged.append(item);
    }

    const bool appsListChanged = (merged.size() != m_apps.size());
    m_apps = merged;

    // 2) 过滤：先按「搜索类型」，再按关键词
    //    "app" 这一档不加扩展名限制（扫进来的本来就是程序，手动条目也想留着）
    const QString category = m_config->searchCategory();
    const QStringList exts =
        Config::categoryExtensions(category).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QSet<QString> allowed(exts.begin(), exts.end());
    const bool typeFiltered = !allowed.isEmpty() && category != QLatin1String("app");

    const QString needle = m_query.trimmed();
    QList<QObject *> filtered;
    filtered.reserve(m_apps.size());
    for (QObject *obj : std::as_const(m_apps)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        if (typeFiltered && !allowed.contains(QFileInfo(item->target()).suffix().toLower()))
            continue;
        if (!needle.isEmpty() && !item->name().contains(needle, Qt::CaseInsensitive)
            && !item->target().contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        filtered.append(obj);
    }
    m_visible = filtered;

    // 3) Everything 全盘结果（可选搜索源，见 doc/2.方案设计.md）
    const QList<QObject *> previous = m_searchResults;
    QList<QObject *> extra;
    if (!needle.isEmpty() && m_config->searchSource() == QLatin1String("everything")
        && m_everything && m_everything->isAvailable()) {
        // 类型集来自搜索栏最左侧那个选择器（Config::categoryExtensions）
        const QStringList hits =
            m_everything->search(needle, 60, Config::categoryExtensions(category));
        QSet<QString> known;
        for (QObject *obj : std::as_const(m_apps)) {
            if (auto *item = qobject_cast<AppItem *>(obj))
                known.insert(QFileInfo(item->target()).absoluteFilePath().toLower());
        }
        for (const QString &hit : hits) {
            if (!allowed.isEmpty() && !allowed.contains(QFileInfo(hit).suffix().toLower()))
                continue; // 兜一道：只留当前搜索类型允许的扩展名
            if (known.contains(QFileInfo(hit).absoluteFilePath().toLower()))
                continue;
            AppItem *item = AppScanner::fromAnyPath(hit, this);
            if (!item)
                continue;
            item->setRemovable(false);
            extra.append(item);
            if (extra.size() >= 24)
                break;
        }
    }

    // 释放上一轮生成的 Everything 临时条目
    for (QObject *obj : std::as_const(previous)) {
        if (!m_apps.contains(obj)) {
            if (auto *item = qobject_cast<AppItem *>(obj))
                item->deleteLater();
        }
    }
    m_searchResults = extra;

    if (appsListChanged)
        emit appsChanged();
    emit visibleAppsChanged();
    emit searchResultsChanged();
}

void Controller::setQuery(const QString &q)
{
    if (m_query == q)
        return;
    m_query = q;
    emit queryChanged();
    m_searchDebounce->start();
}

QList<QObject *> Controller::manualApps() const
{
    QList<QObject *> out;
    out.reserve(m_manual.size());
    for (AppItem *item : m_manual)
        out.append(item);
    return out;
}

// ---- 编辑操作 -------------------------------------------------------------

bool Controller::addPaths(const QList<QUrl> &urls)
{
    int added = 0;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;

        const QString path = url.toLocalFile();
        AppItem *item = AppScanner::fromAnyPath(path, this);
        if (!item) {
            setError(QStringLiteral("无法识别的文件：%1（支持 .lnk / .exe / .url）")
                         .arg(QFileInfo(path).fileName()));
            continue;
        }

        bool duplicate = false;
        for (AppItem *existing : std::as_const(m_manual)) {
            if (existing->id() == item->id()) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            item->deleteLater();
            continue;
        }

        m_manual.append(item);
        ++added;
    }

    if (added > 0) {
        saveManualApps();
        rebuildVisible();
        emit manualAppsChanged();
        setError(QString());
        setStatus(QStringLiteral("已添加 %1 个快捷方式").arg(added));
    }
    return added > 0;
}

void Controller::removeApp(const QString &id)
{
    for (int i = 0; i < m_manual.size(); ++i) {
        if (m_manual.at(i)->id() != id)
            continue;

        AppItem *item = m_manual.takeAt(i);
        item->deleteLater();
        saveManualApps();
        rebuildVisible();
        emit manualAppsChanged();
        setStatus(QStringLiteral("已移除 1 个快捷方式"));
        return;
    }
    setError(QStringLiteral("只有手动添加的条目可以删除"));
}

void Controller::toggleFavorite(const QString &id)
{
    Q_UNUSED(id)
    // 收藏 / 置顶：M5 持久化阶段实现
}

void Controller::moveApp(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_apps.size() || to >= m_apps.size() || from == to)
        return;
    m_apps.move(from, to);
    emit visibleAppsChanged();
    // TODO(M5): 顺序落盘
}

// ---- 启动 -----------------------------------------------------------------

void Controller::launchItem(QObject *obj)
{
    auto *item = qobject_cast<AppItem *>(obj);
    if (!item)
        return;

    QString error;
    if (Launcher::launch(item, &error)) {
        setError(QString());
        setStatus(QStringLiteral("已启动 %1").arg(item->name()));
        if (m_window)
            m_window->hidePanel();
    } else {
        setError(QStringLiteral("启动失败：%1").arg(error));
    }
}

void Controller::openConfigFolder()
{
    const QString dir = QFileInfo(layoutPath()).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void Controller::requestQuit()
{
    QCoreApplication::quit();
}

void Controller::notifyError(const QString &text)
{
    setError(text);
}

bool Controller::setHotkey(const QString &sequence)
{
    if (!m_hotkeys) {
        setError(QStringLiteral("热键管理器未初始化"));
        return false;
    }

    QString error;
    if (!m_hotkeys->setSequence(sequence, &error)) {
        setError(error);
        return false;
    }

    m_config->setHotkey(sequence);
    setError(QString());
    setStatus(QStringLiteral("全局热键已更新为 %1").arg(sequence));
    return true;
}

// ---- 手动条目持久化 -------------------------------------------------------

bool Controller::loadManualApps()
{
    QFile f(layoutPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QJsonArray manual = root.value(QStringLiteral("manual")).toArray();
    for (const QJsonValue &v : manual) {
        AppItem *item = AppItem::fromJson(v.toObject(), this);
        item->setRemovable(true);
        m_manual.append(item);
    }
    return !m_manual.isEmpty();
}

void Controller::saveManualApps()
{
    QJsonArray manual;
    for (AppItem *item : std::as_const(m_manual))
        manual.append(item->toJson());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("manual"), manual);

    const QString path = layoutPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

// ---- 杂项 -----------------------------------------------------------------

QString Controller::appVersion()
{
    return QCoreApplication::applicationVersion();
}

bool Controller::autoStartLaunch() const
{
    return g_autoStartLaunch;
}

void Controller::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit statusTextChanged();
}

void Controller::setError(const QString &text)
{
    if (m_error == text)
        return;
    m_error = text;
    emit errorTextChanged();
}

// 由 main.cpp 在解析命令行后调用
namespace ControllerPrivate {
void setAutoStartLaunch(bool on)
{
    g_autoStartLaunch = on;
}
} // namespace ControllerPrivate
