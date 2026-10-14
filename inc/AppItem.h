#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

/*
 * AppItem —— 启动台里的一个「应用条目」。
 * 是所有层（扫描 / 编辑 / 展示 / 启动）之间唯一的数据单元。
 * 作为 QObject 暴露给 QML，delegate 里可直接读属性。
 */
class AppItem : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id          READ id          CONSTANT)
    Q_PROPERTY(QString name        READ name        WRITE setName        NOTIFY nameChanged)
    Q_PROPERTY(QString kind        READ kind        WRITE setKind        NOTIFY kindChanged)
    Q_PROPERTY(QString target      READ target      WRITE setTarget      NOTIFY targetChanged)
    Q_PROPERTY(QString arguments   READ arguments   WRITE setArguments   NOTIFY argumentsChanged)
    Q_PROPERTY(QString workingDir  READ workingDir  WRITE setWorkingDir  NOTIFY workingDirChanged)
    Q_PROPERTY(QString aumid       READ aumid       WRITE setAumid       NOTIFY aumidChanged)
    Q_PROPERTY(QString sourcePath  READ sourcePath  WRITE setSourcePath  NOTIFY sourcePathChanged)
    Q_PROPERTY(QString iconPath    READ iconPath    WRITE setIconPath    NOTIFY iconPathChanged)
    Q_PROPERTY(QUrl    iconUrl     READ iconUrl                          NOTIFY iconPathChanged)
    Q_PROPERTY(QString category    READ category    WRITE setCategory    NOTIFY categoryChanged)
    Q_PROPERTY(bool    removable   READ removable   WRITE setRemovable   NOTIFY removableChanged)
    Q_PROPERTY(bool    isUwp       READ isUwp                            NOTIFY kindChanged)

public:
    // kind 取值（保持字符串，方便 JSON 持久化与 QML 直接判断）
    static const QString KindDesktop; // 普通桌面程序（由 .lnk / .exe 解析而来）
    static const QString KindUrl;     // 网址快捷方式（.url）
    static const QString KindUwp;     // 商店应用（AUMID）
    static const QString KindManual;  // 手动拖入、未解析出目标的条目

    explicit AppItem(QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString kind() const { return m_kind; }
    QString target() const { return m_target; }
    QString arguments() const { return m_arguments; }
    QString workingDir() const { return m_workingDir; }
    QString aumid() const { return m_aumid; }
    QString sourcePath() const { return m_sourcePath; }
    QString iconPath() const { return m_iconPath; }
    QUrl iconUrl() const;
    QString category() const { return m_category; }
    bool removable() const { return m_removable; }
    bool isUwp() const { return m_kind == KindUwp; }

    void setName(const QString &v);
    void setKind(const QString &v);
    void setTarget(const QString &v);
    void setArguments(const QString &v);
    void setWorkingDir(const QString &v);
    void setAumid(const QString &v);
    void setSourcePath(const QString &v);
    void setIconPath(const QString &v);
    void setCategory(const QString &v);
    void setRemovable(bool v);

    // 稳定 ID：优先 target+AUMID，其次源文件路径，保证多次扫描结果一致
    void rebindId(const QString &seed);

    // 序列化（用于 layout.json）
    QJsonObject toJson() const;
    static AppItem *fromJson(const QJsonObject &obj, QObject *parent = nullptr);

signals:
    void nameChanged();
    void kindChanged();
    void targetChanged();
    void argumentsChanged();
    void workingDirChanged();
    void aumidChanged();
    void sourcePathChanged();
    void iconPathChanged();
    void categoryChanged();
    void removableChanged();

private:
    QString m_id;
    QString m_name;
    QString m_kind;
    QString m_target;
    QString m_arguments;
    QString m_workingDir;
    QString m_aumid;
    QString m_sourcePath;
    QString m_iconPath;
    QString m_category;
    bool m_removable = false;
};
