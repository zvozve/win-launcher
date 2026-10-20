#include "SearchQuery.h"

#include "Config.h"

#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace {

// 程序认识的全部扩展名（各搜索类型并集）。用来判断一个裸词是不是扩展名 ——
// 只认表里的，避免把 "git"、"code" 这种普通词当成扩展名 token。
const QSet<QString> &knownExtSet()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        const QStringList keys = Config::categoryKeys();
        for (const QString &key : keys) {
            const QStringList exts =
                Config::categoryExtensions(key).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &e : exts)
                s.insert(e.toLower());
        }
        return s;
    }();
    return set;
}

// 去掉通配前缀："*.xlsx" / ".xlsx" / "xlsx" 都归到 "xlsx"
QString bareExtension(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1String("*.")))
        s = s.mid(2);
    else if (s.startsWith(QLatin1Char('.')))
        s = s.mid(1);
    return s.toLower();
}

} // namespace

QStringList SearchQuery::knownExtensions()
{
    QStringList out(knownExtSet().begin(), knownExtSet().end());
    out.sort();
    return out;
}

SearchQuery SearchQuery::parse(const QString &text)
{
    SearchQuery q;
    q.m_text = text.trimmed();
    if (q.m_text.isEmpty())
        return q;

    /*
     * 词法：双引号内整体算一个词（Everything 也这么认），其余按空白切。
     * 用 globalMatch 而不是 split，是为了不把引号内的空格切碎。
     */
    static const QRegularExpression wordRe(QStringLiteral("\"([^\"]*)\"|(\\S+)"));

    QStringList exprs;
    QSet<QString> seenExt;

    auto it = wordRe.globalMatch(q.m_text);
    while (it.hasNext()) {
        const auto m = it.next();
        const bool quoted = !m.captured(1).isEmpty();
        const QString word = quoted ? m.captured(1) : m.captured(2);
        if (word.isEmpty())
            continue;

        // 引号内是字面量文件名，不做扩展名识别，原样保留
        if (quoted) {
            q.m_keywords.append(word);
            exprs.append(QStringLiteral("\"%1\"").arg(word));
            continue;
        }

        // ext:pdf;doc / ext:pdf|doc —— Everything 的显式扩展名语法
        if (word.startsWith(QLatin1String("ext:"), Qt::CaseInsensitive)) {
            const QString rest = word.mid(4);
            QStringList parts = rest.split(QRegularExpression(QStringLiteral("[;|]")),
                                           Qt::SkipEmptyParts);
            for (const QString &part : std::as_const(parts)) {
                const QString ext = bareExtension(part);
                if (ext.isEmpty() || seenExt.contains(ext))
                    continue;
                seenExt.insert(ext);
                q.m_extensions.append(ext);
                q.m_keywords.append(ext); // 本地按裸扩展名做子串匹配
            }
            exprs.append(word); // Everything 原样解析
            continue;
        }

        // 友好文件类型词（图片/视频/文档…）→ 展开成 ext: 列表交给 Everything。
        // 「全部 / 所有类型」返回 "*"（不限制扩展名），这个词本身不参与匹配。
        const QString typeExts = Config::expandTypeToken(word);
        if (!typeExts.isEmpty()) {
            if (typeExts != QStringLiteral("*")) {
                exprs.append(QStringLiteral("ext:") + typeExts);
                const QStringList parts =
                    typeExts.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                for (const QString &e : parts) {
                    if (!seenExt.contains(e)) {
                        seenExt.insert(e);
                        q.m_extensions.append(e);
                    }
                }
            }
            continue; // 类型词不进 keywords，也不当裸扩展名
        }

        const QString bare = bareExtension(word);
        // 只有"写了前缀"或"本身在扩展名表里"才算扩展名 token
        const bool looksLikeExt = (bare != word) || knownExtSet().contains(bare);

        q.m_keywords.append(bare);
        exprs.append(bare);

        if (looksLikeExt && !bare.isEmpty() && !seenExt.contains(bare)) {
            seenExt.insert(bare);
            q.m_extensions.append(bare);
        }
    }

    q.m_expression = exprs.join(QLatin1Char(' '));
    return q;
}
