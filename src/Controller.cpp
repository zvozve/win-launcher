#include "Controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QProcess>
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
#include <QSaveFile>
#include <QDateTime>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <utility>

#include "AppItem.h"
#include "AppCategory.h"
#include "AppUninstaller.h"
#include "BackdropBlur.h"
#include "BackdropProvider.h"
#include "DebugTrace.h"
#include "GroupTile.h"
#include "HotkeyManager.h"
#include "IconProvider.h"
#include "Launcher.h"
#include "LauncherWindow.h"
#include "Log.h"
#include "SearchQuery.h"

namespace {

// 每片扫描占用主线程的时间预算：太小会频繁让出事件循环，
// 太大会让面板卡顿。25ms 是「跟手」与「扫得快」的折中。
constexpr int kScanSliceMs = 25;

/**
 * 内存快照（工作集 / 私有提交）。
 * 扫描结束时打一次：图标全部生成并加载进场景之后的那一跳，是「两百多 MB」
 * 里唯一靠少做点事就能降下来的部分（其余是 Qt 引擎与渲染后端的固定开销）。
 */
QString memStat()
{
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                             sizeof(pmc))) {
        return QStringLiteral("ws=%1MB private=%2MB")
                .arg(pmc.WorkingSetSize / (1024 * 1024))
                .arg(pmc.PrivateUsage / (1024 * 1024));
    }
    return QStringLiteral("n/a");
}

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

/*
 * 上次扫描结果的缓存（启动秒开用）。
 *
 * 放在配置目录而不是 cache/icons：图标缓存会被「重建图标」整批清掉，
 * 应用清单不该跟着消失。版本号变了就当没有缓存，重新扫一遍。
 */
QString appsCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("apps-cache.json"));
}

constexpr int kAppsCacheVersion = 1;

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

    /*
     * 顶部分类标签点一下就改 AppConfig.appCategory —— 这是**运行时的界面状态**，
     * 不是「下次启动才生效」的配置，所以必须立刻重算过滤结果。
     * 这个连接放在构造函数里（在 attachWindow 之前），就是为了防止「先点分类、
     * 后接信号」的窗口期。
     */
    connect(m_config, &Config::appCategoryChanged, this, &Controller::rebuildVisible);
    // 归组阈值改了要立刻重新折叠（设置窗口里是个数字选择）
    connect(m_config, &Config::groupThresholdChanged, this, &Controller::rebuildVisible);

    loadLayout();
    /*
     * 先把上次的扫描结果读进来再刷新界面：面板一出来就有完整网格和「共 N 个」，
     * 不用干等扫描。真正的扫描稍后由 main 在后台触发（见 main.cpp 的 rescan），
     * 扫完在 finishScan 里比对，没变化就不动界面。
     */
    loadCachedApps();
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

    // 让窗口的 Esc 处理能先退出文件夹（见 LauncherWindow::event）
    m_window->setController(this);

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

void Controller::showPanel()
{
    if (!m_window)
        return;
    if (m_window->isVisible()) {
        // 已经开着就别再 toggle（会关掉），只把它提到最前
        m_window->raise();
        wlTrace(QStringLiteral("showPanel: 面板已可见，提到最前"));
        return;
    }
    wlTrace(QStringLiteral("showPanel: 面板隐藏中 -> 弹出"));
    m_window->showAtCursor();
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

    /*
     * 有缓存时**不写**扫描进度状态行：用户已经能看到「共 152 个」，
     * 再把它换成「正在扫描 0 / 311…」就是倒退。进度只走 scanProgress 属性
     * （设置界面里的进度条会用到），状态行保持缓存给的数字。
     */
    if (!m_cacheLoaded) {
        setStatus(QStringLiteral("正在扫描 0 / %1…").arg(m_scanTasks.size()));
    } else {
        wlTrace(QStringLiteral("rescan: 后台校验开始（%1 个任务，界面保持缓存内容）")
                    .arg(m_scanTasks.size()));
    }
    QTimer::singleShot(0, this, &Controller::processScanChunk);
}

void Controller::processScanChunk()
{
    QElapsedTimer slice;
    slice.start();

    int lastReported = -1;
    while (m_scanIndex < m_scanTasks.size() && slice.elapsed() < kScanSliceMs) {
        if (m_scanIndex < 3)
            // 每个任务一行, 348 个任务就是 348 行纯噪声 —— 只有 --trace
            // 时才需要这种粒度(排查"为什么某个应用没被扫到")。
            if (wlTraceEnabled())
                wlTrace(QStringLiteral("task #%1: %2").arg(m_scanIndex).arg(m_scanTasks.at(m_scanIndex).path));

        ScannedApp app;
        if (AppScanner::runTask(m_scanTasks.at(m_scanIndex), &app))
            m_scanBuffer.append(app);
        ++m_scanIndex;

        // 每 20 个（且至少隔一片）刷一次进度，避免刷爆信号
        if (m_scanIndex % 20 == 0 && lastReported != m_scanIndex) {
            lastReported = m_scanIndex;
            if (!m_cacheLoaded) // 有缓存时状态行留给「共 N 个」，别退回扫描计数
                setStatus(QStringLiteral("正在扫描 %1 / %2…")
                              .arg(m_scanIndex)
                              .arg(m_scanTasks.size()));
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

    /*
     * 和**当前显示的内容**比对：一致就跳过重建，界面一动不动。
     *
     * 这是有缓存时的常态 —— 启动已用上次清单渲染好了（用户抱怨的「每次启动先
     * 扫描、数字从 0 跳」就是这里该避免的），后台扫完如果发现没变化，还去
     * qDeleteAll + 重建 138 个格子纯属自找：会丢滚动位置、闪烁、重新解一遍图标。
     *
     * 比对键用 **target**（不是 id）：AppItem::rebindId 把 seed 存成 SHA1，
     * id() 是哈希值、和 ScannedApp.seed 永远不相等（踩过：比对恒 false，
     * 每次启动都白重建一遍）。target 在两边都是原始路径，可以直接对上。
     * 同时比 name / group / category —— 名字、归组、分类任一变了都要重建。
     */
    bool unchanged = false;
    if (m_cacheLoaded && unique.size() == m_all.size()) {
        QHash<QString, QString> shown;
        shown.reserve(m_all.size());
        for (AppItem *item : std::as_const(m_all)) {
            const QString key = item->target().isEmpty() ? item->name() : item->target().toLower();
            shown.insert(key,
                         item->name() + QLatin1Char('|') + item->group() + QLatin1Char('|')
                             + item->category());
        }
        unchanged = true;
        for (const ScannedApp &a : std::as_const(unique)) {
            const QString key = a.target.isEmpty() ? a.name : a.target.toLower();
            const auto it = shown.constFind(key);
            if (it == shown.constEnd()
                || it.value()
                       != a.name + QLatin1Char('|') + a.group + QLatin1Char('|') + a.category) {
                unchanged = false;
                break;
            }
        }
        if (unchanged)
            wlTrace(QStringLiteral("finishScan: %1 项与当前显示一致，跳过界面重建")
                        .arg(unique.size()));
    }

    if (!unchanged) {
        /*
         * ★ 旧对象必须 deleteLater，不能 qDeleteAll 立即删。
         *
         * 此刻 m_grid 里的 GroupTile、m_apps 里的指针**都还指着这些对象**
         * （rebuildVisible 要到后面才重建）。立即 delete 会让它们全变野指针，
         * 一旦 QML 读 gridItems / 搜索里遍历 m_apps，qobject_cast 解引用就
         * 段错误 —— 实测「搜 1.4 pdf」进程直接崩（exit 139），表现为用户说的
         * 「卡死」。deleteLater 把销毁推回事件循环空闲，中间这些指针仍然有效。
         */
        const QList<AppItem *> old = m_all;
        m_all.clear();
        m_all.reserve(unique.size());
        for (const ScannedApp &app : std::as_const(unique))
            m_all.append(AppScanner::toItem(app, this));

        for (AppItem *item : old) {
            item->setParent(nullptr);
            item->deleteLater();
        }
    }

    m_scanBuffer.clear();
    m_scanTasks.clear();
    m_scanIndex = 0;

    m_scanning = false;
    wlTrace(QStringLiteral("finishScan: 内存 %1").arg(memStat()));
    emit scanningChanged();
    emit scanProgressChanged();

    // 落盘供下次启动秒开；扫描结果本身就是唯一真相，这里不比对旧缓存文件
    saveCachedApps(unique);
    m_cacheLoaded = true; // 之后的 rescan 都算「已有内容在显示」

    if (!unchanged) {
        rebuildVisible();
        // 状态行的「共 N 个」由 rebuildVisible 统一给（数量按**格子**算，文件夹算一格），
        // 这里再写一次 m_all.size() 会把折叠后的数字盖回「221 个应用」。
        wlTrace(QStringLiteral("finishScan: %1 items（已重建界面）").arg(m_all.size()));
    }
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

    // 4) 过滤：应用分类（仅空查询浏览用）-> 关键词
    //     搜索类型选择器已移除 —— 输入即走 Everything，文件类型由查询里的类型词决定。
    const QString appCat = m_config->appCategory();
    const bool categoryFiltered = !appCat.isEmpty() && appCat != QLatin1String("all");

    const SearchQuery query = SearchQuery::parse(m_query);
    const QString needle = query.text();
    // 是否带了文件名关键词：仅类型词/扩展名（如只输「图片」）时不回退本地应用网格
    const bool hasText = !query.keywords().isEmpty();

    /*
     * 关键词匹配是 AND 语义：`报告 xlsx` 要求两个词都命中。
     * 命中范围是 name 或 target，而 target 是完整路径（含扩展名），
     * 所以 "报告 xlsx" 能直接匹配 D:\…\报告.xlsx，不用再单独判扩展名。
     */
    const QStringList kws = query.keywords();

    QList<QObject *> matched;
    matched.reserve(m_apps.size());
    for (QObject *obj : std::as_const(m_apps)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        if (categoryFiltered && item->category() != appCat)
            continue;
        /*
         * 空查询（浏览模式）→ 全部显示，这是启动后的默认面貌。
         * 有输入但只含类型词/扩展名（如只输「图片」）→ 不回退本地网格：
         * 那种查询要的结果在全盘，回退会把 221 个应用全塞进网格。
         */
        if (!hasText && !m_query.trimmed().isEmpty())
            continue;
        bool hit = true;
        for (const QString &kw : kws) {
            if (item->name().contains(kw, Qt::CaseInsensitive)
                || item->target().contains(kw, Qt::CaseInsensitive)) {
                continue;
            }
            hit = false;
            break;
        }
        if (!hit)
            continue;
        matched.append(obj);
    }
    m_visible = matched;

    /*
     * 4.5) 归组：把「应用条目」折成网格格子。
     *
     * 文件夹只在**没有搜索词**时出现。理由：搜索是「我要找某个东西」，
     * 结果被收进文件夹里看不到名字反而更差；而且全盘搜索（Everything）的命中
     * 本来就是平铺的，两边不一致会让人以为搜丢了。
     */
    QString openGroup = m_openGroup;
    /*
     * 搜索时**不再**在这里自动退出文件夹。
     *
     * 以前是 `if (!needle.isEmpty()) openGroup.clear();`，后果是：搜索结果里
     * 折叠出来的文件夹（例如搜 "E-book" 时 calibre 那组 4 个全中）点进去，
     * openGroupKey 刚把 m_openGroup 设好，紧接着这次 rebuild 又把它清掉 ——
     * 表现就是「搜索出来的文件夹点不了」，点了没反应。
     * 现在「退回顶层」只发生在 setQuery 真的换搜索词的时候（见 setQuery）。
     */

    QList<QObject *> tiles;
    if (!openGroup.isEmpty()) {
        /*
         * 进到文件夹里：只列这一组的成员，不再嵌套文件夹。
         *
         * ★ 必须包成 GroupTile 再塞进 tiles —— 下面「重建 model」那步只收
         *   qobject_cast<GroupTile*> 成功的对象，直接 append(item) 的话这些
         *   AppItem 会被整批丢掉，网格变成 0 格（2026-10-19 实测：
         *   `grid: 0 tiles ... open=calibre... (0)`，点文件夹一片空白）。
         */
        for (QObject *obj : std::as_const(matched)) {
            if (auto *item = qobject_cast<AppItem *>(obj)) {
                if (item->group() == openGroup)
                    tiles.append(new GroupTile(item, this));
            }
        }
        // 组空了（成员全被移除 / 分类切走）就自动退回顶层
        if (tiles.isEmpty())
            openGroup.clear();
    }
    if (openGroup.isEmpty())
        rebuildGroups(matched, &tiles);

    /*
     * 重建网格 model 的所有权：上一轮的 GroupTile 整批换掉。
     *
     * 用 deleteLater 而不是 delete：QML 的 GridView 可能还要引用旧的瓦片到本轮
     * 事件循环结束（这是 Qt 官方对「删掉 model 里的 QObject」的推荐做法）。
     * 顺手把 parent 摘掉，避免 Controller 析构时再删一次。
     */
    for (GroupTile *tile : std::as_const(m_grid)) {
        tile->setParent(nullptr);
        tile->deleteLater();
    }
    m_grid.clear();
    m_grid.reserve(tiles.size());
    for (QObject *obj : std::as_const(tiles)) {
        if (auto *tile = qobject_cast<GroupTile *>(obj)) {
            tile->setParent(this);
            m_grid.append(tile);
        }
    }

    /*
     * 状态行跟着筛选走，否则按了「开发」下面还写「共 221 个应用」，
     * 看起来就像筛选没生效。动作消息（已启动 X / 已移除 X）不由这里管，
     * 它们在下一次筛选变化前一直留着。
     *
     * ★ 数字用**格子数**（m_grid.size()）而不是应用数：一个文件夹在网格里
     *   只占一格，计数也只算一次（用户明确要求）。所以 221 个应用折成 152 格时
     *   这里写 152；进文件夹后写成员数。
     *
     * ★ 有搜索词时**延后到第 5 步之后再设**：全盘命中的数字那时候才算出来，
     *   否则搜「1.4 pdf」会写「匹配 · 0 个」，而下面明明躺着 8 个文件。
     */
    const int shownTiles = m_grid.size();
    if (needle.isEmpty())
        setStatus(categoryFiltered
                      ? QStringLiteral("分类「%1」· %2 个")
                            .arg(AppCategory::label(appCat))
                            .arg(shownTiles)
                      : QStringLiteral("共 %1 个").arg(shownTiles));

    /*
     * ★ 这里不比对「变了没有」：openGroupKey / closeGroup 都是「先改 m_openGroup
     *   再调 rebuildVisible」，到这里 m_openGroup 早就等于 openGroup，拿它算
     *   groupChanged 永远 false —— openGroupChanged 一次都不会发。后果（实测）：
     *   QML 里的 Ctrl.openGroup 永远停在空串，面包屑不出现，Esc 也退不出文件夹。
     *   所以信号无条件发（多刷新几个绑定，代价可以忽略）。
     */
    m_openGroup = openGroup;

    /*
     * 网格内容的打点：归组是「看得见但不好断言」的东西，--trace 里留一份
     * 「N 格 / M 个文件夹」+ 前几个文件夹的名字，排查时不用猜。
     */
    {
        int folderCount = 0;
        QStringList folders;
        for (GroupTile *tile : std::as_const(m_grid)) {
            if (!tile->isFolder())
                continue;
            ++folderCount;
            if (folders.size() < 6)
                folders.append(QStringLiteral("%1(%2)").arg(tile->label()).arg(tile->count()));
        }
        wlTrace(QStringLiteral("grid: %1 tiles = %2 apps + %3 folders [%4]%5")
                    .arg(m_grid.size())
                    .arg(m_grid.size() - folderCount)
                    .arg(folderCount)
                    .arg(folders.join(QStringLiteral(", ")))
                    .arg(openGroup.isEmpty()
                             ? QString()
                             : QStringLiteral("  open=%1 (%2)").arg(openGroupLabel()).arg(m_grid.size())));
    }

    /*
     * 5) Everything 全盘结果
     *
     * 输入即走全盘（搜索类型选择器已移除）：词非空且 Everything 可用就查。
     * 查询串直接交给 Everything —— 类型词已在 SearchQuery 展开成 ext:…，
     * 显式扩展名按 Everything 原生 AND 子串匹配。
     * Everything64.dll 缺失时 wantDisk 为 false，退回上面 matched 的纯本地过滤。
     */
    const QList<QObject *> previous = m_searchResults;
    QList<QObject *> extra;
    // 输入即走 Everything 全盘（搜索类型选择器已移除）；Everything 不可用时
    // 退回纯本地应用过滤（上面 matched 已在做，仅按文件名关键词匹配）。
    const bool wantDisk = !needle.isEmpty() && m_everything && m_everything->isAvailable();
    m_diskSearch = wantDisk;

    if (wantDisk) {
        /*
         * 查询串直接交给 Everything：类型词（图片/视频…）已在 SearchQuery 里
         * 展开成 ext:…；显式扩展名按 Everything 原生 AND 子串匹配；引号保持字面量。
         */
        const QString expr = query.expression().isEmpty() ? needle : query.expression();
        wlTrace(QStringLiteral("disk: query='%1'").arg(expr));
        const QList<EverythingHit> hits = m_everything->search(expr, 96, QString());
        wlTrace(QStringLiteral("disk: search -> %1 hits").arg(hits.size()));
        /*
         * ★ 必须先落到具名变量再取 begin/end。
         *
         * 写成 `QSet<QString> s(query.extensions().begin(), query.extensions().end())`
         * 是未定义行为：extensions() 按值返回，两次调用得到**两个不同的临时容器**，
         * 迭代器不属于同一容器。空列表时 begin/end 碰巧相等还看不出来，
         * 一旦非空（「1.4 pdf」）就在构造 QSet 时段错误 —— 用户报的「卡死」。
         */
        const QStringList extList = query.extensions();
        const QSet<QString> allowed(extList.begin(), extList.end());

        // 已扫到的应用（按 target）不重复出现在「全盘」里
        QSet<QString> known;
        for (QObject *obj : std::as_const(m_apps)) {
            if (auto *item = qobject_cast<AppItem *>(obj))
                known.insert(item->target().toLower());
        }

        int skipExt = 0;
        int skipKnown = 0;
        int skipFolder = 0;
        for (const EverythingHit &hit : hits) {
            if (hit.isFolder) {
                ++skipFolder;
                continue;
            }
            if (!allowed.isEmpty() && !allowed.contains(hit.extension.toLower())) {
                ++skipExt;
                continue; // 兜一道：只留当前类型（或用户写的扩展名）允许的
            }
            if (known.contains(hit.fullPath.toLower())) {
                ++skipKnown;
                continue;
            }

            /*
             * ★ 这里**绝不碰磁盘**：不 stat、不解析 .lnk、不提 jumbo 图标。
             *
             * 全盘命中可能上百条，而 Everything 索引里有离线/断开的卷 ——
             * 对每条做一次 QFileInfo::exists() / SHGetFileInfo 就会卡在卷的超时上，
             * 整个 GUI 冻住（「搜 1.4 pdf 卡死」的根因）。
             *
             * 展示要的东西全从索引取：大小 = Everything_SetRequestFlags(SIZE)，
             * 扩展名/路径 = 纯字符串拆分，图标 = 按扩展名的关联图标（只查注册表）。
             */
            auto *item = new AppItem(this);
            item->setName(hit.fileName);
            item->setKind(AppItem::KindManual);
            item->setTarget(hit.fullPath);
            item->setWorkingDir(hit.dirPath);
            item->setSourcePath(hit.fullPath);
            item->setCategory(AppCategory::classify(hit.fileName, hit.fullPath));
            wlTrace(QStringLiteral("disk: hit '%1' ext=%2 size=%3")
                        .arg(hit.fileName, hit.extension)
                        .arg(hit.size));
            item->setIconPath(IconProvider::associatedIcon(hit.extension));
            wlTrace(QStringLiteral("disk: hit icon ok"));
            item->setSizeBytes(hit.size);
            item->setRemovable(false);
            item->rebindId(hit.fullPath.toLower());
            extra.append(item);
            if (extra.size() >= 48)
                break;
        }
        wlTrace(QStringLiteral("diskHits: hits=%1 extra=%2 skipExt=%3 skipKnown=%4 skipFolder=%5")
                    .arg(hits.size())
                    .arg(extra.size())
                    .arg(skipExt)
                    .arg(skipKnown)
                    .arg(skipFolder));
    }

    // 释放上一轮生成的 Everything 临时条目
    for (QObject *obj : std::as_const(previous)) {
        if (!m_apps.contains(obj)) {
            if (auto *item = qobject_cast<AppItem *>(obj))
                item->deleteLater();
        }
    }
    m_searchResults = extra;

    // 有搜索词时的状态行（全盘命中数这会儿才知道，见上面那段注释）
    if (!needle.isEmpty()) {
        setStatus(extra.isEmpty()
                      ? QStringLiteral("匹配「%1」· %2 个").arg(needle).arg(shownTiles)
                      : (shownTiles > 0
                             ? QStringLiteral("匹配「%1」· 本地 %2 + 全盘 %3 个")
                                   .arg(needle)
                                   .arg(shownTiles)
                                   .arg(extra.size())
                             : QStringLiteral("匹配「%1」· 全盘 %2 个").arg(needle).arg(extra.size())));
    }

    /*
     * 搜索打点：解析结果（关键词 / 显式扩展名）、走的哪条路、两侧命中数。
     * 「原生搜索」这类语法不好靠肉眼判断对不对，--trace 里留一份。
     */
    if (!needle.isEmpty()) {
        wlTrace(QStringLiteral("search: '%1' kw=[%2] ext=[%3] disk=%4 localHits=%5 diskHits=%6")
                    .arg(needle)
                    .arg(query.keywords().join(QLatin1Char(',')))
                    .arg(query.extensions().join(QLatin1Char(',')))
                    .arg(wantDisk ? 1 : 0)
                    .arg(shownTiles)
                    .arg(extra.size()));
    }

    if (appsListChanged)
        emit appsChanged();
    emit visibleAppsChanged();
    emit gridItemsChanged();
    emit openGroupChanged();   // 无条件发，理由见上面 m_openGroup 那段注释
    emit searchResultsChanged();
}

/*
 * 归组：把过滤后的应用列表折成「应用 + 文件夹」的格子列表。
 *
 * 规则（都是从本机实测数据反推的）：
 *
 *   1. 只有开始菜单里有目录结构的才有 group（注册表条目一律为空）；
 *   2. **组内成员必须全部在列表里**才折叠。搜索 / 分类筛选下经常只剩组里的
 *      一两条，这时如果还显示文件夹，点进去只有一个图标 —— 比不折叠更糟；
 *   3. 成员数 >= groupThreshold（默认 3）才折叠。本机 2 个一组的很多
 *      （卸载残留、同软件的主程序 + 帮助），折起来反而多一次点击；
 *   4. 文件夹由**筛选后的列表**直接折出来，自身不存分类；按「开发」标签时
 *      J-Link / MSYS2 这些组只要成员全在该分类里就照常折叠（2026-10-19 修正：
 *      早先还额外记过一个「主导分类」用来切顶部标签，已随 openGroupKey 一起删掉）。
 *
 * 输出顺序：按「组内第一个成员」在输入列表里的位置排列，保持扫描顺序，
 * 分组不会让网格跳来跳去。
 */
void Controller::rebuildGroups(const QList<QObject *> &matched, QList<QObject *> *tilesOut)
{
    if (!tilesOut)
        return;

    const int threshold = qMax(2, m_config->groupThreshold());

    // 按归组键收集（保持首次出现的顺序）
    QHash<QString, QList<AppItem *>> buckets;
    QStringList order;
    QHash<QString, int> total; // 该组在**完整基础列表**里的成员数（判断是否被筛掉过）

    for (QObject *obj : std::as_const(m_apps)) {
        if (auto *item = qobject_cast<AppItem *>(obj)) {
            if (!item->group().isEmpty())
                ++total[item->group()];
        }
    }
    for (QObject *obj : std::as_const(matched)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        const QString key = item->group();
        if (key.isEmpty())
            continue;
        if (!buckets.contains(key))
            order.append(key);
        buckets[key].append(item);
    }

    QSet<QString> folded;
    for (const QString &key : std::as_const(order)) {
        const QList<AppItem *> members = buckets.value(key);
        if (members.size() < threshold)
            continue;
        if (members.size() < total.value(key))
            continue; // 组里有成员被筛掉了（搜索 / 分类），不折叠
        folded.insert(key);
    }

    // 输出：先按输入顺序走一遍，遇到该折叠的组只在第一次出现时放一个文件夹瓦片
    QSet<QString> emitted;
    for (QObject *obj : std::as_const(matched)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        const QString key = item->group();
        if (key.isEmpty() || !folded.contains(key)) {
            tilesOut->append(new GroupTile(item, this));
            continue;
        }
        if (emitted.contains(key))
            continue; // 组已经在前面放过了
        emitted.insert(key);
        tilesOut->append(GroupTile::makeFolder(key, item->groupLabel(), buckets.value(key), this));
    }

    /*
     * 按名字排序（用户要求「按字母排」）。
     *
     * 放在归组之后、对**最终格子列表**排一次：文件夹用它的组名、应用用应用名，
     * 两边混在同一序列里比较，所以 A 开头的应用不会被 12 个文件夹全挤到后面。
     * localeAwareCompare 而不是裸 <=：中文名按拼音排，纯 ASCII 名按字母排，
     * 直接比 code point 的话中文会被丢到最末尾。
     */
    std::stable_sort(tilesOut->begin(), tilesOut->end(), [](QObject *a, QObject *b) {
        auto *ta = qobject_cast<GroupTile *>(a);
        auto *tb = qobject_cast<GroupTile *>(b);
        if (!ta || !tb)
            return false;
        return QString::localeAwareCompare(ta->label(), tb->label()) < 0;
    });
}

QList<QObject *> Controller::gridItems() const
{
    QList<QObject *> out;
    out.reserve(m_grid.size());
    for (GroupTile *tile : m_grid)
        out.append(tile);
    return out;
}

QString Controller::openGroupLabel() const
{
    if (m_openGroup.isEmpty())
        return QString();
    // 和文件夹瓦片用同一套命名规则（末段是 bin / Sysdiag 这类技术目录名时往上一层）
    for (QObject *obj : std::as_const(m_apps)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (item && item->group() == m_openGroup)
            return item->groupLabel();
    }
    const int slash = m_openGroup.lastIndexOf(QLatin1Char('\\'));
    return slash < 0 ? m_openGroup : m_openGroup.mid(slash + 1);
}

/*
 * 进文件夹。**不碰顶部分类**。
 *
 * 2026-10-19 修正：以前这里会把 appCategory 切成「组内主导分类」，想让顶部标签
 * 高亮成该分类、提示用户在哪一层。实际效果是「点一下文件夹，顶部分类跳到别的
 * 标签」——而且 appCategory 是持久化到 config.json 的，返回顶层也不会还原，
 * 用户原本的筛选被悄悄改掉了。面包屑（‹ 全部应用 / 文件夹名）已经足够提示层级，
 * 分类筛选应该只由用户按标签决定。
 *
 * 另外：能看到的文件夹，其成员必定全部在当前筛选结果里（rebuildGroups 的折叠
 * 前提），所以进来之后不需要再切分类也不会是空网格。
 */
void Controller::openGroupKey(const QString &groupKey)
{
    if (groupKey.isEmpty() || groupKey == m_openGroup)
        return;

    // 组不存在（可能刚被清掉）就别动，避免进入一个空文件夹
    bool exists = false;
    for (QObject *obj : std::as_const(m_apps)) {
        if (auto *item = qobject_cast<AppItem *>(obj)) {
            if (item->group() == groupKey) {
                exists = true;
                break;
            }
        }
    }
    if (!exists)
        return;

    m_openGroup = groupKey;
    rebuildVisible();
}

void Controller::closeGroup()
{
    if (m_openGroup.isEmpty())
        return;
    m_openGroup.clear();
    rebuildVisible();
}

/*
 * 某个分类在网格里会占几格 —— 也就是「把文件夹折起来之后」的数量。
 *
 * 规则和 rebuildGroups 完全一致（阈值、以及「组内成员必须全在」），只是这里
 * 按分类分别算，用来填顶部分类标签上的数字：
 *   tiles = 该分类的应用数 - 被折进文件夹的成员数 + 文件夹个数
 * 于是「全部 221」会显示成 152 格，和网格里数出来的格子数一致；
 * 一个文件夹只算一次（用户明确要求）。
 *
 * 不叠搜索词/搜索类型的影响：分类标签上的数字一直是全局量，不跟着搜索跳
 * （沿用原来的约定）。
 */
int Controller::tileCountFor(const QString &categoryKey) const
{
    const int threshold = qMax(2, m_config->groupThreshold());
    const bool noFilter = categoryKey.isEmpty() || categoryKey == QLatin1String("all");

    QHash<QString, int> groupTotal; // 该组在**全部应用**里的成员数
    QHash<QString, int> groupInCat; // 该组在这个分类下的成员数
    int appsInCat = 0;

    for (QObject *obj : std::as_const(m_apps)) {
        auto *item = qobject_cast<AppItem *>(obj);
        if (!item)
            continue;
        const bool inCat = noFilter || item->category() == categoryKey;
        if (inCat)
            ++appsInCat;
        const QString g = item->group();
        if (g.isEmpty())
            continue;
        ++groupTotal[g];
        if (inCat)
            ++groupInCat[g];
    }

    int folders = 0;
    int appsInFolded = 0;
    for (auto it = groupInCat.constBegin(); it != groupInCat.constEnd(); ++it) {
        const int n = it.value();
        // 和 rebuildGroups 同一个折叠前提：达到阈值，且成员一个都没被筛掉
        if (n >= threshold && n == groupTotal.value(it.key())) {
            ++folders;
            appsInFolded += n;
        }
    }

    return appsInCat - appsInFolded + folders;
}

/*
 * 顶部分类标签：[{key, label, count}]。
 * count 是**格子数**（文件夹算一格），针对「剔除隐藏后的基础列表」，
 * 不含搜索词的影响 —— 搜到一半时标签上的数字还是全局数量，不会跳来跳去。
 */
void Controller::rebuildCategories()
{
    const QStringList keys = AppCategory::keys();
    const QStringList labels = AppCategory::labels();

    QVariantList cats;
    cats.reserve(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), keys.at(i));
        m.insert(QStringLiteral("label"), labels.value(i));
        m.insert(QStringLiteral("count"), tileCountFor(keys.at(i)));
        cats.append(m);
    }

    m_tileTotal = tileCountFor(QStringLiteral("all"));
    m_appCategories = cats;
    emit appCategoriesChanged();
}

void Controller::setQuery(const QString &q)
{
    if (m_query == q)
        return;
    m_query = q;

    /*
     * 换搜索词就退回顶层：搜索是「我要找某个东西」，被锁在某个文件夹里搜
     * 容易让人以为搜漏了。
     *
     * ★ 只能在这里退，不能在 rebuildVisible 里按 needle 判断 —— 那会把
     *   「在搜索结果里点文件夹进去」这个动作也一起打回顶层，表现为文件夹点不了。
     *   点进文件夹后搜索词不变，所以这里不会再被清掉。
     */
    m_openGroup.clear();

    emit queryChanged();
    m_searchDebounce->start();
}

/*
 * 外部（QML 面板复位时）强制重算一次可见列表。
 *
 * 为什么不复用 setQuery("")：那个函数在值没变时会直接 return，不触发重算；
 * 面板复位的场景恰恰需要「值没变也重算一次」——比如某次过滤因为别的原因把
 * 列表清空了，只改搜索词是救不回来的。
 */
void Controller::refreshVisible()
{
    // 停掉待触发的防抖，避免紧接着又来一次重复计算
    m_searchDebounce->stop();
    rebuildVisible();
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

void Controller::uninstallApp(QObject *obj)
{
    auto *item = qobject_cast<AppItem *>(obj);
    if (!item)
        return;

    const AppUninstaller::Entry entry = AppUninstaller::findFor(item);
    if (!entry.isValid()) {
        setError(QStringLiteral("没找到「%1」的卸载入口（绿色软件和商店应用没有）")
                     .arg(item->name()));
        return;
    }

    QString error;
    if (!AppUninstaller::run(entry, &error)) {
        setError(QStringLiteral("卸载失败：%1").arg(error));
        return;
    }

    setError(QString());
    setStatus(QStringLiteral("正在卸载 %1…卸完记得「重新扫描」清掉图标")
                  .arg(entry.displayName.isEmpty() ? item->name() : entry.displayName));
    if (m_window)
        m_window->hidePanel();
}

void Controller::roundWindow(QObject *obj, qreal radius)
{
    auto *w = qobject_cast<QWindow *>(obj);
    if (!w)
        return;

    const auto apply = [w, radius] {
        if (!w->winId())
            return;
        const QScreen *scr = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
        const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
        WindowEffects::setRoundedRegion(w->winId(), radius, dpr);
    };

    apply();

    // 尺寸一变区域就失效了，自动跟着重裁（只接一次）
    if (!w->property("_wlRoundedHooked").toBool()) {
        w->setProperty("_wlRoundedHooked", true);
        QObject::connect(w, &QWindow::widthChanged, w, apply);
        QObject::connect(w, &QWindow::heightChanged, w, apply);
    }
}

void Controller::openConfigFolder()
{
    const QString dir = QFileInfo(layoutPath()).absolutePath();
    QDir().mkpath(dir);
    // 用资源管理器直接打开目录，比 QDesktopServices::openUrl 可靠：GUI 子系统程序里
    // 后者偶尔静默失败，表现为「点了没反应」。
    const QString native = QDir::toNativeSeparators(dir);
    if (!QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << native)) {
        // 兜底：explorer 起不来再退回 openUrl
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

void Controller::requestQuit()
{
    // 退出原因必须留痕。托盘「退出」、QML 的 Qt.quit()、以及以后任何新增
    // 的退出入口都会走到这里; 抓一次栈, 万一不是用户点的, 栈会指出来。
    wl::log::warn(QStringLiteral("requestQuit: 收到退出请求"));
    wl::log::stackTrace(QStringLiteral("requestQuit 调用栈"), 2);
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

// ---- 扫描结果缓存（启动秒开）----------------------------------------------

void Controller::loadCachedApps()
{
    QFile f(appsCachePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // 版本不符就当没有缓存（字段可能变了，宁可贵一次全扫）
    if (root.value(QStringLiteral("version")).toInt() != kAppsCacheVersion)
        return;

    const QJsonArray arr = root.value(QStringLiteral("apps")).toArray();
    if (arr.isEmpty())
        return;

    QList<ScannedApp> apps;
    apps.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ScannedApp a;
        a.name = o.value(QStringLiteral("name")).toString();
        a.kind = o.value(QStringLiteral("kind")).toString();
        a.target = o.value(QStringLiteral("target")).toString();
        a.arguments = o.value(QStringLiteral("args")).toString();
        a.workingDir = o.value(QStringLiteral("workdir")).toString();
        a.sourcePath = o.value(QStringLiteral("source")).toString();
        a.iconPath = o.value(QStringLiteral("icon")).toString();
        a.category = o.value(QStringLiteral("category")).toString();
        a.group = o.value(QStringLiteral("group")).toString();
        a.seed = o.value(QStringLiteral("seed")).toString();
        a.removable = o.value(QStringLiteral("removable")).toBool();
        if (!a.name.isEmpty())
            apps.append(a);
    }
    if (apps.isEmpty())
        return;

    m_all.reserve(m_all.size() + apps.size());
    for (const ScannedApp &a : std::as_const(apps))
        m_all.append(AppScanner::toItem(a, this));

    m_cacheLoaded = true;
    wlTrace(QStringLiteral("cache: 载入 %1 项（启动即渲染，扫描转后台校验）").arg(m_all.size()));
}

void Controller::saveCachedApps(const QList<ScannedApp> &apps) const
{
    QJsonArray arr;
    for (const ScannedApp &a : std::as_const(apps)) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), a.name);
        o.insert(QStringLiteral("kind"), a.kind);
        o.insert(QStringLiteral("target"), a.target);
        o.insert(QStringLiteral("args"), a.arguments);
        o.insert(QStringLiteral("workdir"), a.workingDir);
        o.insert(QStringLiteral("source"), a.sourcePath);
        o.insert(QStringLiteral("icon"), a.iconPath);
        o.insert(QStringLiteral("category"), a.category);
        o.insert(QStringLiteral("group"), a.group);
        o.insert(QStringLiteral("seed"), a.seed);
        o.insert(QStringLiteral("removable"), a.removable);
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kAppsCacheVersion);
    root.insert(QStringLiteral("savedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("apps"), arr);

    const QString path = appsCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile：写临时文件再原子替换，进程被杀不会留下半个坏 JSON
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        wlTrace(QStringLiteral("cache: 写入失败 %1").arg(path));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        wlTrace(QStringLiteral("cache: commit 失败 %1").arg(path));
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
