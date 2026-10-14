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
    Q_PROPERTY(QString searchSource READ searchSource WRITE setSearchSource NOTIFY searchSourceChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool followSystemTheme READ followSystemTheme WRITE setFollowSystemTheme
                   NOTIFY followSystemThemeChanged)
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY gridColumnsChanged)
    Q_PROPERTY(QString searchCategory READ searchCategory WRITE setSearchCategory
                   NOTIFY searchCategoryChanged)
    // 启动台顶部的应用分类（dev / media / office / internet / utility / other / all）。
    // 和上面的 searchCategory 不是一回事：那个是「搜索类型」（应用/文档/图片…），
    // 这个是「应用的类别」，值域见 AppCategory::keys()。
    Q_PROPERTY(QString appCategory READ appCategory WRITE setAppCategory
                   NOTIFY appCategoryChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)

public:
    explicit Config(QObject *parent = nullptr);

    void load();

    QString hotkey() const { return m_hotkey; }
    bool autostart() const { return m_autostart; }
    bool hideOnFocusOut() const { return m_hideOnFocusOut; }
    bool includeRegistry() const { return m_includeRegistry; }
    QString searchSource() const { return m_searchSource; }
    bool darkMode() const { return m_darkMode; }
    bool followSystemTheme() const { return m_followSystemTheme; }
    int gridColumns() const { return m_gridColumns; }
    QString searchCategory() const { return m_searchCategory; }
    QString appCategory() const { return m_appCategory; }
    QString configPath() const;

    void setHotkey(const QString &v);
    void setAutostart(bool v);
    void setHideOnFocusOut(bool v);
    void setIncludeRegistry(bool v);
    void setSearchSource(const QString &v);
    void setDarkMode(bool v);
    void setFollowSystemTheme(bool v);
    void setGridColumns(int v);
    void setSearchCategory(const QString &v);
    void setAppCategory(const QString &v);

    // ---- 搜索类型（本地过滤 + Everything 的 ext: 过滤共用同一张表）----
    // key: app / doc / image / video / audio / archive / all
    static QStringList categoryKeys();
    static QStringList categoryLabels();
    static QString categoryLabel(const QString &key);
    // 分号分隔的扩展名；返回空串表示不按类型过滤（all）
    static QString categoryExtensions(const QString &key);

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
    void searchSourceChanged();
    void darkModeChanged();
    void followSystemThemeChanged();
    void gridColumnsChanged();
    void searchCategoryChanged();
    void appCategoryChanged();

private:
    void syncDarkMode();

    QString m_hotkey;
    bool m_autostart = false;
    bool m_hideOnFocusOut = true;
    bool m_includeRegistry = true;
    QString m_searchSource = QStringLiteral("local");
    bool m_darkMode = false;
    bool m_followSystemTheme = true;
    int m_gridColumns = 7;
    QString m_searchCategory = QStringLiteral("app");
    QString m_appCategory = QStringLiteral("all");
};
