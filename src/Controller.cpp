#include "Controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include <utility>

#include "AppItem.h"
#include "AppCategory.h"
#include "BackdropBlur.h"
#include "BackdropProvider.h"
#include "DebugTrace.h"
#include "HotkeyManager.h"
#include "IconProvider.h"
#include "Launcher.h"
#include "LauncherWindow.h"

namespace {

// 每片扫描占用主线程的时间预算：太小会频繁让出事件循环，
// 太大会让面板卡顿。25ms 是「跟手」与「扫得快」的折中。
constexpr int kScanSliceMs = 25;

// 面板玻璃底图：半径/降采样/饱和度都是「逻辑像素 / 倍数」意义上的参数
//   36px 的模糊半径是照 macOS vibrancy 的观感调的；降采样只影响速度不影响强度；
//   饱和度 1.6 对应 macOS 那层 backdrop-filter: blur() saturate()。
constexpr int kBackdropBlurRadius = 36;
constexpr int kBackdropDownsample = 4;
constexpr qreal kBackdropSaturation = 1.6;

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

    loadLayout();
    rebuildVisible();

    setStatus(QStringLiteral("就绪"));
}

// ---- 初始化 ---------------------------------------------------------------

void Controller::attachBackdropProvider(BackdropProvider *provider)
{
    m_backdrop = provider;
}

/*
 * 抓屏 + 模糊 + 圆角 + 外阴影，生成面板底图。
 *
 * 时间点很关键：必须在窗口 show() 之前调，此刻屏幕上是「没有本窗口」的干净画面。
 * 底图覆盖的范围是**整个窗口**（面板 + 四周阴影边距），因为阴影要画在面板外面。
 *
 * 传入的是窗口的物理像素矩形：不要改成传 Qt 的逻辑坐标再乘 dpr，多屏不同 DPR 时
 * 那个换算不成立（详见 LauncherWindow::showAtCursor）。
 *
 * 成本：窗口约 1330×830 物理像素，降采样 4 倍后只有 330×200，3 次 box blur 是
 * 毫秒级，整个函数通常 10~50ms，不会明显拖慢弹出。
 */
void Controller::refreshBackdrop(const QRect &windowRectPx, qreal cornerRadiusLogical, qreal dpr)
{
    if (!m_backdrop || windowRectPx.width() <= 1 || windowRectPx.height() <= 1)
        return;

    QElapsedTimer timer;
    timer.start();

    const QImage shot = BackdropBlur::grabScreen(windowRectPx);
    if (shot.isNull()) {
        wlTrace("backdrop: 抓屏失败，沿用上一张（或退化为纯色玻璃）");
        return;
    }

    BackdropBlur::PanelOptions opt;
    // 面板在底图里的位置 = 窗口矩形内缩一个阴影边距
    const int marginPx = qRound(LauncherWindow::kShadowMargin * (dpr > 0.0 ? dpr : 1.0));
    opt.panelRectInImage = shot.rect().adjusted(marginPx, marginPx, -marginPx, -marginPx);
    opt.radiusPx = cornerRadiusLogical;
    opt.dpr = dpr;
    opt.blurRadiusPx = kBackdropBlurRadius;
    opt.downsample = kBackdropDownsample;
    opt.saturation = kBackdropSaturation;

    const QImage image = BackdropBlur::buildPanelBackdrop(shot, opt);
    if (image.isNull()) {
        wlTrace("backdrop: 合成底图失败");
        return;
    }

    m_backdrop->setImage(image);
    ++m_backdropToken;
    emit backdropChanged();

    // 排查用：把底图落盘，直接看圆角 / 阴影 / 裁断对不对
    if (qEnvironmentVariableIsSet("WINLAUNCHER_DUMP_BACKDROP")) {
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("backdrop-dump.png"));
        image.save(path, "PNG");
    }

    wlTrace(QStringLiteral("backdrop: canvas=%1x%2 dpr=%3 blur=%4 ds=%5 sat=%6 r=%7 in %8ms")
                .arg(image.width())
                .arg(image.height())
                .arg(dpr)
                .arg(kBackdropBlurRadius)
                .arg(kBackdropDownsample)
                .arg(kBackdropSaturation)
                .arg(cornerRadiusLogical)
                .arg(timer.elapsed()));
}

void Controller::clearBackdrop()
{
    if (!m_backdrop)
        return;
    m_backdrop->setImage(QImage());
    ++m_backdropToken;
    emit backdropChanged();
}

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

    // 同步连接：窗口就是靠这个在 show() 之前把底图抓好的，异步会来不及。
    connect(m_window, &LauncherWindow::backdropRequested, this,
            [this](const QRect &windowRectPx, qreal radiusLogical, qreal dpr) {
                refreshBackdrop(windowRectPx, radiusLogical, dpr);
            });
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

    // 2) 剔掉被用户从启动台移除的（layout.json 的 hidden 名单，可在设置里恢复）
    QList<QObject *> shown;
    shown.reserve(merged.size());
    for (QObject *obj : std::as_const(merged)) {
        if (auto *item = qobject_cast<AppItem *>(obj)) {
            if (m_hidden.contains(item->id()))
                continue;
        }
        shown.append(obj);
    }

    const bool appsListChanged = (shown.size() != m_apps.size());
    m_apps = shown;

    // 3) 顶部分类标签的数量（跟着上面的列表走）
    rebuildCategories();

    // 4) 过滤：应用分类 -> 搜索类型 -> 关键词
    //    注意两个 category 不是一回事：
    //      m_config->appCategory()    应用类别（开发/影音/办公…）
    //      m_config->searchCategory() 搜索类型（应用/文档/图片…），它决定扩展名白名单
    const QString appCat = m_config->appCategory();
    const bool categoryFiltered = !appCat.isEmpty() && appCat != QLatin1String("all");

    const QString searchCat = m_config->searchCategory();
    const QStringList exts =
        Config::categoryExtensions(searchCat).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QSet<QString> allowed(exts.begin(), exts.end());
    // "app" 这一档不加扩展名限制（扫进来的本来就是程序，手动条目也想留着）
    const bool typeFiltered = !allowed.isEmpty() && searchCat != QLatin1String("app");

    const QString needle = m_query.trimmed();
    QList<QObject *> filtered;
    filtered.reserve(m_apps.size());
    for (QObject *obj : std::as_const(m_apps)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        if (categoryFiltered && item->category() != appCat)
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

    // 5) Everything 全盘结果（可选搜索源，见 doc/2.方案设计.md）
    const QList<QObject *> previous = m_searchResults;
    QList<QObject *> extra;
    if (!needle.isEmpty() && m_config->searchSource() == QLatin1String("everything")
        && m_everything && m_everything->isAvailable()) {
        // 类型集来自搜索栏最左侧那个选择器（Config::categoryExtensions）
        const QStringList hits =
            m_everything->search(needle, 60, Config::categoryExtensions(searchCat));
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

/*
 * 顶部分类标签：[{key, label, count}]。
 * count 针对的是「剔除隐藏后的基础列表」，不含搜索词的影响 —— 搜到一半时标签上的
 * 数字还是全局数量，不会跳来跳去。
 */
void Controller::rebuildCategories()
{
    QHash<QString, int> counts;
    for (QObject *obj : std::as_const(m_apps)) {
        if (auto *item = qobject_cast<AppItem *>(obj))
            ++counts[item->category()];
    }

    const QStringList keys = AppCategory::keys();
    const QStringList labels = AppCategory::labels();

    QVariantList cats;
    cats.reserve(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        const QString key = keys.at(i);
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        m.insert(QStringLiteral("label"), labels.value(i));
        m.insert(QStringLiteral("count"),
                 key == QLatin1String("all") ? m_apps.size() : counts.value(key));
        cats.append(m);
    }

    m_appCategories = cats;
    emit appCategoriesChanged();
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
        saveLayout();
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
        saveLayout();
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

void Controller::runAsAdmin(QObject *obj)
{
    auto *item = qobject_cast<AppItem *>(obj);
    if (!item)
        return;

    QString error;
    if (Launcher::runAsAdmin(item, &error)) {
        setError(QString());
        setStatus(QStringLiteral("已以管理员身份启动 %1").arg(item->name()));
        if (m_window)
            m_window->hidePanel();
    } else {
        setError(QStringLiteral("提权启动失败：%1").arg(error));
    }
}

void Controller::revealInExplorer(QObject *obj)
{
    auto *item = qobject_cast<AppItem *>(obj);
    if (!item)
        return;

    QString error;
    if (Launcher::revealInExplorer(item, &error)) {
        setError(QString());
        // 资源管理器会盖住面板，直接收起来
        if (m_window)
            m_window->hidePanel();
    } else {
        setError(error);
    }
}

void Controller::hideApp(QObject *obj)
{
    auto *item = qobject_cast<AppItem *>(obj);
    if (!item)
        return;

    m_hidden.insert(item->id());
    saveLayout();
    rebuildVisible();
    emit hiddenCountChanged();
    setStatus(QStringLiteral("已从启动台移除「%1」（设置里可以恢复）").arg(item->name()));
}

void Controller::unhideAll()
{
    if (m_hidden.isEmpty())
        return;

    const int n = m_hidden.size();
    m_hidden.clear();
    saveLayout();
    rebuildVisible();
    emit hiddenCountChanged();
    setStatus(QStringLiteral("已恢复 %1 个图标").arg(n));
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

// ---- 布局持久化（layout.json：手动条目 + 隐藏名单）-------------------------

bool Controller::loadLayout()
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

    const QJsonArray hidden = root.value(QStringLiteral("hidden")).toArray();
    for (const QJsonValue &v : hidden) {
        const QString id = v.toString();
        if (!id.isEmpty())
            m_hidden.insert(id);
    }

    return !m_manual.isEmpty() || !m_hidden.isEmpty();
}

void Controller::saveLayout()
{
    QJsonArray manual;
    for (AppItem *item : std::as_const(m_manual))
        manual.append(item->toJson());

    QJsonArray hidden;
    for (const QString &id : std::as_const(m_hidden))
        hidden.append(id);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("manual"), manual);
    root.insert(QStringLiteral("hidden"), hidden);

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
