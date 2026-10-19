#pragma once

#include <QString>
#include <QStringList>

/*
 * 搜索词解析：支持 Everything 的「原生」写法 —— 空格分隔多个词、词里直接带扩展名。
 *
 *     报告 xlsx          文件名里同时含「报告」和「xlsx」
 *     "季度 报告" pdf    引号内整体算一个词
 *     ext:pdf;doc        显式扩展名（Everything 语法）
 *     *.xlsx             通配写法，等价于 xlsx
 *
 * 解析结果有两个用途：
 *
 *   keywords()   给本地过滤用（每个词都必须命中 name 或 target，AND 语义）。
 *                扩展名 token 会规范成裸扩展名（xlsx / pdf），因为 target 是完整
 *                路径、本身就含 ".xlsx"，子串匹配天然成立。
 *   expression() 给 EverythingSearch 用。保留 ext: 原样、保留引号，其它按空格拼 ——
 *                Everything 自己就认这套语法，我们不做二次解释。
 *
 * 为什么要识别扩展名 token：搜索栏的类型选择器（应用/文档/图片…）会往 Everything
 * 的查询后头追加 " ext:..."。用户明确写了 xlsx 时，再追加一份类型白名单既多余,
 * 还会互相打架（选着「应用」却敲 xlsx，永远 0 结果）。识别到了就不再追加。
 */
class SearchQuery
{
public:
    // 空查询（text() 为空串）时 keywords/expression/extensions 均为空
    static SearchQuery parse(const QString &text);

    // 程序认识的全部扩展名（各搜索类型的并集，小写、不带点）
    static QStringList knownExtensions();

    QString text() const { return m_text; }
    QString expression() const { return m_expression; }
    QStringList keywords() const { return m_keywords; }

    // 用户显式写出来的扩展名，小写、不带点、已去重
    QStringList extensions() const { return m_extensions; }
    bool hasExtensionToken() const { return !m_extensions.isEmpty(); }

    bool isEmpty() const { return m_text.isEmpty(); }

private:
    QString m_text;
    QString m_expression;
    QStringList m_keywords;
    QStringList m_extensions;
};
