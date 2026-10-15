#pragma once

#include <QFont>
#include <QObject>
#include <QString>

/*
 * UiFont —— QML 侧要用的界面字体信息。
 *
 * 为什么必须在 C++ 里问：右键菜单的图标用的是 Windows 11 自带的
 * **Segoe Fluent Icons**（SegoeIcons.ttf），字形本身就在系统字体里，
 * 不用打包任何图片资源。但这台机器上装没装只有 QFontDatabase 知道 ——
 * 万一没有（Server 版、精简版），字形会渲染成豆腐块，
 * 所以 QML 得能问一句「有吗」，没有就退回纯文字菜单。
 */
class UiFont : public QObject
{
    Q_OBJECT

    // 菜单图标字体的 family（不可用时为空串）
    Q_PROPERTY(QString menuIconFamily READ menuIconFamily CONSTANT)
    // 有没有可用的 Fluent 图标字体
    Q_PROPERTY(bool menuIconsAvailable READ menuIconsAvailable CONSTANT)

public:
    explicit UiFont(QObject *parent = nullptr);

    QString menuIconFamily() const { return m_family; }
    bool menuIconsAvailable() const { return !m_family.isEmpty(); }

    /*
     * 菜单图标字形（Segoe Fluent Icons 的私有区码点）。
     * 名字按语义取，取值是照着字体里的字形逐个挑出来的 —— 见
     * doc/2.方案设计.md §7.6「右键菜单图标」。
     */
    enum Glyph {
        Open = 0xE8B7,      // 应用窗口
        Admin = 0xE8DA,     // 带锁的盾牌（UAC）
        Reveal = 0xE838,    // 打开的文件夹
        Uninstall = 0xE74D, // 垃圾桶
        Remove = 0xE8E6,    // 虚线框（从启动台移除，不是删除）
    };
    Q_ENUM(Glyph)

private:
    QString m_family;
};
