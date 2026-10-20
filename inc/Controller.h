#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "AppScanner.h"

// 属性里用到 Config* / EverythingSearch*，元类型要求类型完整，不能只前置声明
#include "Config.h"
#include "EverythingSearch.h"

class AppItem;
class BackdropProvider;
class GroupTile;
class HotkeyManager;
class LauncherWindow;
class QTimer;

/*
 * 前后端唯一的门面（QML 里叫 Ctrl）。
 *
 * 职责：
 *   - 持有应用列表、当前搜索词、过滤结果
 *   - 驱动扫描（GUI 线程分片执行，见 AppScanner 注释）
 *   - 承接编辑界面的增删改
 *   - 把热键 / 托盘的动作广播给 QML
 * QML 不直接碰 core 里的类，全部走这里。
 */
class Controller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QList<QObject *> apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(QList<QObject *> visibleApps READ visibleApps NOTIFY visibleAppsChanged)
    /*
     * 网格真正的 model：应用 + 折叠文件夹的混合列表（见 GroupTile）。
     * 和 visibleApps 的关系：visibleApps 永远是「应用条目」（托盘/编辑界面/计数用），
     * gridItems 才是面板上要画的东西 —— 文件夹瓦片只在里面。
     */
    Q_PROPERTY(QList<QObject *> gridItems READ gridItems NOTIFY gridItemsChanged)
    /*
     * 当前打开的文件夹归组键；空串 = 网格在顶层。
     * ★ 进文件夹**不会**改分类筛选（早先会切成「组内主导分类」，结果点一下
     *   文件夹顶部分类标签就跳到别的分类，而且会持久化）。层级提示交给面包屑。
     */
    Q_PROPERTY(QString openGroup READ openGroup NOTIFY openGroupChanged)
    Q_PROPERTY(QString openGroupLabel READ openGroupLabel NOTIFY openGroupChanged)
    Q_PROPERTY(QList<QObject *> manualApps READ manualApps NOTIFY manualAppsChanged)
    Q_PROPERTY(QList<QObject *> searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY appsChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY visibleAppsChanged)
    /*
     * 网格里的**格子数**（一个文件夹算一格）。面板上的计数都该用这个，
     * 而不是应用条数 —— 221 个应用折成 152 格时，用户眼里的「数量」是 152。
     */
    Q_PROPERTY(int tileCount READ tileCount NOTIFY gridItemsChanged)
    // 全部格子数（不看分类/搜索），给状态栏「命中 / 总数」当分母
    Q_PROPERTY(int tileTotal READ tileTotal NOTIFY appCategoriesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    /*
     * 本次搜索是否走了全盘（Everything）。没有「搜索源」开关了，源是按搜索类型
     * 自动推导的（应用档=本地，其它档=全盘，写扩展名=全盘），QML 靠这个值
     * 决定空网格时提示「全盘没找到」还是「本地没有可显示的」。
     */
    Q_PROPERTY(bool diskSearch READ diskSearch NOTIFY searchResultsChanged)
    Q_PROPERTY(int scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(Config *config READ config CONSTANT)
    Q_PROPERTY(EverythingSearch *everything READ everything CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool autoStartLaunch READ autoStartLaunch CONSTANT)
    // 面板背景图的版本号：每抓一次新背景 +1，QML 用 "image://wlbg/panel?<token>"
    // 取图（带 token 才不会被 QML 的图片缓存拦住）
    Q_PROPERTY(int backdropToken READ backdropToken NOTIFY backdropChanged)
    /*
     * 顶部分类标签的数据源：每项 { key, label, count }。
     * count 是「当前可见基础列表」里属于该分类的条目数，随着搜索词变化重算。
     */
    Q_PROPERTY(QVariantList appCategories READ appCategories NOTIFY appCategoriesChanged)
    // 被用户从启动台移除（进 hidden 名单）的条目数，设置界面用
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY hiddenCountChanged)

public:
    explicit Controller(Config *config, EverythingSearch *everything, QObject *parent = nullptr);

    void attachWindow(LauncherWindow *window);
    void attachHotkeys(HotkeyManager *hotkeys);
    // provider 的所有权归 QQmlEngine（addImageProvider 会接管），这里只记指针
    void attachBackdropProvider(BackdropProvider *provider);

    // ---- QML 可调用 ----
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void launchItem(QObject *item);
    Q_INVOKABLE bool addPaths(const QList<QUrl> &urls);
    Q_INVOKABLE void removeApp(const QString &id);
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void moveApp(int from, int to);
    Q_INVOKABLE void openConfigFolder();
    Q_INVOKABLE void clearCacheAndRescan();
    Q_INVOKABLE void requestQuit();
    Q_INVOKABLE void notifyError(const QString &text);
    Q_INVOKABLE bool setHotkey(const QString &sequence);
    /*
     * 强制重算一次可见列表。给 QML 在「面板复位」这类场景用：搜索词/分类都回到
     * 默认值之后，即便值本身没变化也要重新过一次过滤，否则列表可能停在空状态。
     */
    Q_INVOKABLE void refreshVisible();

    // ---- 文件夹（同源多入口归组）----
    /*
     * 点文件夹瓦片：进去，网格换成这一组里的应用。
     *
     * ★ 名字不能叫 openGroup：类里已经有一个同名的 Q_PROPERTY
     *   （QString openGroup = 当前归组键），QML 侧**属性会遮蔽同名方法**，
     *   写 `Ctrl.openGroup(key)` 会报
     *   "Property 'openGroup' of object Controller is not a function"，
     *   点文件夹瓦片完全没反应。所以方法叫 openGroupKey。
     */
    Q_INVOKABLE void openGroupKey(const QString &groupKey);
    // 返回键 / 面包屑：回到顶层
    Q_INVOKABLE void closeGroup();

    // ---- 图标右键菜单 ----
    Q_INVOKABLE void runAsAdmin(QObject *item);       // 以管理员身份启动（弹 UAC）
    Q_INVOKABLE void revealInExplorer(QObject *item); // 在资源管理器中定位
    /*
     * 「从启动台移除」：只把 id 加进 hidden 名单（layout.json），**不删文件、不删条目**，
     * 所以是可恢复的 —— 设置界面里有「恢复全部」。
     * 扫描出来的条目尤其不能真删：下次扫描它又回来了。
     */
    Q_INVOKABLE void hideApp(QObject *item);
    Q_INVOKABLE void unhideAll();
    /*
     * 「卸载此程序」：从注册表 Uninstall 键找这个条目对应的卸载项，执行它的
     * UninstallString（剩下的交给软件自己的卸载向导）。
     * 找不到卸载项时只是提示，不会去强删文件 —— 强删是另一码事，风险大得多。
     */
    Q_INVOKABLE void uninstallApp(QObject *item);

    /*
     * 给独立窗口（设置窗口这类 frameless Window）加 Win11 那样的 8px 圆角。
     * 只在 winId 之后就绪，所以 QML 要在 visible / 尺寸变化时调一次；
     * 内部做了防重复连接，重复调没有副作用。
     */
    Q_INVOKABLE void roundWindow(QObject *window, qreal radius);

    /*
     * 抓取窗口区域（物理像素，Win32 坐标）的桌面像素，模糊 + 裁圆角 + 烘外阴影后
     * 交给 QML 当面板底图。必须在窗口 show() 之前调用，否则会抓到自己的上一帧。
     * 抓图失败（远程桌面、区域越界等）时保持上一次的图，QML 会退化成纯色玻璃。
     */
    void refreshBackdrop(const QRect &windowRectPx, qreal cornerRadiusLogical, qreal dpr);
    // 丢掉当前底图（下次弹出前会重新抓）
    void clearBackdrop();

    // ---- getters ----
    QList<QObject *> apps() const { return m_apps; }
    QList<QObject *> visibleApps() const { return m_visible; }
    QList<QObject *> gridItems() const;
    QString openGroup() const { return m_openGroup; }
    QString openGroupLabel() const;
    QList<QObject *> manualApps() const;
    QList<QObject *> searchResults() const { return m_searchResults; }
    QString query() const { return m_query; }
    int totalCount() const { return m_apps.size(); }
    int matchCount() const { return m_visible.size(); }
    int tileCount() const { return m_grid.size(); }
    int tileTotal() const { return m_tileTotal; }
    bool scanning() const { return m_scanning; }
    bool diskSearch() const { return m_diskSearch; }
    int scanProgress() const { return m_scanIndex; }
    int scanTotal() const { return m_scanTasks.size(); }
    QString statusText() const { return m_status; }
    QString errorText() const { return m_error; }
    Config *config() const { return m_config; }
    EverythingSearch *everything() const { return m_everything; }
    static QString appVersion();
    bool autoStartLaunch() const;

    // 供 QQmlEngine::addImageProvider 注册（image://wlbg/）
    BackdropProvider *backdropProvider() const { return m_backdrop; }
    int backdropToken() const { return m_backdropToken; }
    QVariantList appCategories() const { return m_appCategories; }
    int hiddenCount() const { return m_hidden.size(); }

    void setQuery(const QString &q);

signals:
    void appsChanged();
    void visibleAppsChanged();
    void gridItemsChanged();
    void openGroupChanged();
    void manualAppsChanged();
    void searchResultsChanged();
    void queryChanged();
    void scanningChanged();
    void scanProgressChanged();
    void statusTextChanged();
    void errorTextChanged();
    void backdropChanged();
    void appCategoriesChanged();
    void hiddenCountChanged();

    // 给 QML 的「外部触发」信号（托盘 / 热键）
    void showPanelRequested();
    void editRequested();
    void settingsRequested();

public slots:
    void togglePanel();

private slots:
    void onHotkeyActivated();
    void processScanChunk();

private:
    void beginScan();
    void finishScan();
    void rebuildVisible();
    void setStatus(const QString &text);
    void setError(const QString &text);
    void rebuildCategories();
    // 某个分类折叠后占几格（文件夹算一格）；空串 / "all" = 全部
    int tileCountFor(const QString &categoryKey) const;
    /*
     * 把「应用列表」折成网格格子的步骤：按 group 归组，成员数 >= 阈值的收成文件夹，
     * 其余的保持单条目。groupless 的写法是「组内所有成员都进来了才折叠」——
     * 搜索/分类筛选下不会出现「文件夹里只剩 1 个」的壳。
     */
    void rebuildGroups(const QList<QObject *> &matched, QList<QObject *> *tilesOut);
    bool loadLayout();   // layout.json：手动条目 + 隐藏名单
    void saveLayout();

    /*
     * 扫描结果缓存（启动秒开）。
     *   loadCachedApps()  启动时同步读入上次清单，面板一出来就有内容；
     *   saveCachedApps()  finishScan 时落盘，供下次启动用。
     * 后台扫描照常进行，finishScan 里比对：和当前显示一致就**不动界面**，
     * 有差异才换数据重建 —— 见 finishScan 里那段「等价则跳过重建」。
     */
    void loadCachedApps();
    void saveCachedApps(const QList<ScannedApp> &apps) const;

    Config *m_config = nullptr;
    EverythingSearch *m_everything = nullptr;
    LauncherWindow *m_window = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    BackdropProvider *m_backdrop = nullptr;
    QTimer *m_searchDebounce = nullptr;
    int m_backdropToken = 0;

    QList<AppItem *> m_all;      // 扫描得到的（不含手动项）
    QList<AppItem *> m_manual;   // 用户拖入/手工添加的
    QList<QObject *> m_apps;     // 合并 + 剔除隐藏后的基础列表
    QList<QObject *> m_visible;  // 应用条目：分类 + 搜索过滤后（不含文件夹）
    QList<GroupTile *> m_grid;   // 网格 model：应用 + 文件夹（GroupTile，归本类所有）
    QString m_openGroup;         // 当前打开的文件夹归组键，空 = 顶层
    QList<QObject *> m_searchResults;
    QSet<QString> m_hidden;      // 用户从启动台移除的条目 id（可恢复）
    QVariantList m_appCategories; // [{key,label,count}]，供顶部分类标签用
    int m_tileTotal = 0;          // 全部格子数（折叠后的总数）

    QList<AppScanner::Task> m_scanTasks;
    QList<ScannedApp> m_scanBuffer;
    int m_scanIndex = 0;

    QString m_query;
    QString m_status;
    QString m_error;
    bool m_scanning = false;
    // 本轮启动是否已从缓存恢复清单：true 时扫描只是后台校验，
    // 状态行/空态提示都不该再报「正在扫描 x / y」。
    bool m_cacheLoaded = false;
    bool m_diskSearch = false;   // 本轮搜索是否走了全盘（见 diskSearch 属性）
};
