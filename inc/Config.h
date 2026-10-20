#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

/*
 * 配置中心：全部落在 %APPDATA%/WinLauncher/config.json。
 * 面板/编辑界面直接绑属性，改动即时保存。
 */
class Config : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString hotkey READ hotkey WRITE setHotkey NOTIFY hotkeyChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(bool hideOnFocusOut READ hideOnFocusOut WRITE setHideOnFocusOut
                   NOTIFY hideOnFocusOutChanged)
    Q_PROPERTY(bool includeRegistry READ includeRegistry WRITE setIncludeRegistry
                   NOTIFY includeRegistryChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool followSystemTheme READ followSystemTheme WRITE setFollowSystemTheme
                   NOTIFY followSystemThemeChanged)
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY gridColumnsChanged)
    // 启动台顶部的应用分类（dev / media / office / internet / utility / other / all），
    // 只用于空查询时浏览已装应用，不进入搜索逻辑。
    Q_PROPERTY(QString appCategory READ appCategory WRITE setAppCategory
                   NOTIFY appCategoryChanged)
    /*
     * 同一个开始菜单文件夹里至少这么多条才折叠成一个文件夹（默认 3）。
     * 见 Controller::rebuildGroups：本机 2 个一组的多半是「主程序 + 卸载/帮助」，
     * 折起来反而多一次点击。
     */
    Q_PROPERTY(int groupThreshold READ groupThreshold WRITE setGroupThreshold
                   NOTIFY groupThresholdChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)

public:
    explicit Config(QObject *parent = nullptr);

    void load();

    QString hotkey() const { return m_hotkey; }
    bool autostart() const { return m_autostart; }
    bool hideOnFocusOut() const { return m_hideOnFocusOut; }
    bool includeRegistry() const { return m_includeRegistry; }
    bool darkMode() const { return m_darkMode; }
    bool followSystemTheme() const { return m_followSystemTheme; }
    int gridColumns() const { return m_gridColumns; }
    QString appCategory() const { return m_appCategory; }
    int groupThreshold() const { return m_groupThreshold; }
    QString configPath() const;

    void setHotkey(const QString &v);
    void setAutostart(bool v);
    void setHideOnFocusOut(bool v);
    void setIncludeRegistry(bool v);
    void setDarkMode(bool v);
    void setFollowSystemTheme(bool v);
    void setGridColumns(int v);
    void setAppCategory(const QString &v);
    void setGroupThreshold(int v);

    // ---- 文件类型 → 扩展名映射（搜索时把「图片」等类型词展开成 ext:…）----
    // key: app / doc / image / video / audio / archive / all
    static QStringList categoryKeys();
    static QStringList categoryLabels();
    static QString categoryLabel(const QString &key);
    // 分号分隔的扩展名；返回空串表示不按类型过滤（all）
    static QString categoryExtensions(const QString &key);
    // 友好类型词（中文标签或英文 key，如「图片」/image）展开成扩展名列表（分号分隔）；
    // 返回 "*" 表示「全部 / 所有类型」（不限制扩展名）；返回空串表示不是类型词。
    static QString expandTypeToken(const QString &token);

    // 读注册表判断系统是否深色（AppsUseLightTheme == 0）
    static bool systemUsesDarkMode();
    static bool isAutostartEnabled();
    static void setAutostartEnabled(bool on);

    QJsonObject toJson() const;
    void save();

signals:
    void hotkeyChanged();
    void autostartChanged();
    void hideOnFocusOutChanged();
    void includeRegistryChanged();
    void darkModeChanged();
    void followSystemThemeChanged();
    void gridColumnsChanged();
    void appCategoryChanged();
    void groupThresholdChanged();

private:
    void syncDarkMode();

    QString m_hotkey;
    bool m_autostart = false;
    bool m_hideOnFocusOut = true;
    bool m_includeRegistry = true;
    bool m_darkMode = false;
    bool m_followSystemTheme = true;
    int m_gridColumns = 7;
    QString m_appCategory = QStringLiteral("all");
    int m_groupThreshold = 2;
};
