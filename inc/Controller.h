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
    Q_PROPERTY(QList<QObject *> manualApps READ manualApps NOTIFY manualAppsChanged)
    Q_PROPERTY(QList<QObject *> searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY appsChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY visibleAppsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(Config *config READ config CONSTANT)
    Q_PROPERTY(EverythingSearch *everything READ everything CONSTANT)
    Q_PROPERTY(QStringList searchCategoryLabels READ searchCategoryLabels CONSTANT)
    Q_PROPERTY(QStringList searchCategoryKeys READ searchCategoryKeys CONSTANT)
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
    QList<QObject *> manualApps() const;
    QList<QObject *> searchResults() const { return m_searchResults; }
    QString query() const { return m_query; }
    int totalCount() const { return m_apps.size(); }
    int matchCount() const { return m_visible.size(); }
    bool scanning() const { return m_scanning; }
    int scanProgress() const { return m_scanIndex; }
    int scanTotal() const { return m_scanTasks.size(); }
    QString statusText() const { return m_status; }
    QString errorText() const { return m_error; }
    Config *config() const { return m_config; }
    EverythingSearch *everything() const { return m_everything; }
    QStringList searchCategoryLabels() const { return Config::categoryLabels(); }
    QStringList searchCategoryKeys() const { return Config::categoryKeys(); }
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
    bool loadLayout();   // layout.json：手动条目 + 隐藏名单
    void saveLayout();

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
    QList<QObject *> m_visible;  // 分类 + 搜索过滤后
    QList<QObject *> m_searchResults;
    QSet<QString> m_hidden;      // 用户从启动台移除的条目 id（可恢复）
    QVariantList m_appCategories; // [{key,label,count}]，供顶部分类标签用

    QList<AppScanner::Task> m_scanTasks;
    QList<ScannedApp> m_scanBuffer;
    int m_scanIndex = 0;

    QString m_query;
    QString m_status;
    QString m_error;
    bool m_scanning = false;
};
