#pragma once

#include <QString>
#include <QStringList>

/*
 * 应用分类 —— 启动台顶部那排分类标签（对齐 macOS Applications 面板的
 * Social / Creativity / Productivity & Finance / Utilities / Other）。
 *
 * Windows 上**没有** App Store 那种 category 元数据，只能按名字 + 目标路径做关键词匹配。
 * 规则表和优先级都集中在这里，方便维护；命中不了就归 "other"，不硬塞。
 *
 * 两个约定：
 *   1. kRules 的顺序就是优先级，先命中先赢 —— 所以「开发」要排在「工具」前面：
 *      "Visual Studio Code" 既像开发也像工具，应该归开发。
 *   2. 匹配对象是 "名字 + 目标路径" 两段拼起来再转小写。带上路径很有用：
 *      C:\Program Files\Git\... 这种光看名字看不出来的也能归对。
 */
namespace AppCategory {

// 含 "all"（不过滤）。keys() 与 labels() 一一对应。
QStringList keys();
QStringList labels();
QString label(const QString &key);

// 返回分类 key；永远不会返回 "all"
QString classify(const QString &name, const QString &targetPath);

} // namespace AppCategory
