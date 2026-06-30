#include "gittide/ui/syntaxhighlighter.hpp"

#include <QColor>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

using namespace KSyntaxHighlighting;

namespace gittide::ui
{

namespace
{

/// AbstractHighlighter that assembles one HTML string per highlighted line.
/// applyFormat() is called for formatted spans; gaps between spans (and any
/// trailing remainder) are emitted as escaped, default-coloured text.
class HtmlCollector : public AbstractHighlighter
{
public:
    void beginLine(const QString& text)
    {
        m_text = text;
        m_html.clear();
        m_cursor = 0;
    }

    QString endLine()
    {
        if (m_cursor < m_text.size())
            m_html += m_text.mid(m_cursor).toHtmlEscaped();
        return m_html;
    }

    /// Highlight @p ln starting from @p state; returns the new state.
    State highlight(const QString& ln, const State& state)
    {
        return highlightLine(ln, state);
    }

protected:
    void applyFormat(int offset, int length, const Format& format) override
    {
        if (length <= 0)
            return;
        if (offset < m_cursor)
            return;
        if (offset > m_cursor)
            m_html += m_text.mid(m_cursor, offset - m_cursor).toHtmlEscaped();

        const QString seg = m_text.mid(offset, length).toHtmlEscaped();
        const QColor  col = format.textColor(theme());
        if (col.isValid())
            m_html += QStringLiteral("<span style=\"color:%1\">%2</span>").arg(col.name(), seg);
        else
            m_html += seg;
        m_cursor = offset + length;
    }

private:
    QString m_text;
    QString m_html;
    int     m_cursor = 0;
};

} // namespace

SyntaxHighlighter::SyntaxHighlighter()
    : m_repo(std::make_unique<Repository>())
{
}

SyntaxHighlighter::~SyntaxHighlighter() = default;

bool SyntaxHighlighter::hasDefinition(const QString& filePath) const
{
    return m_repo->definitionForFileName(filePath).isValid();
}

std::vector<QString> SyntaxHighlighter::highlightLines(const QString& filePath,
                                                       const std::vector<QString>& lines,
                                                       bool dark) const
{
    const Definition def = m_repo->definitionForFileName(filePath);
    if (!def.isValid())
        return {};

    HtmlCollector hl;
    hl.setDefinition(def);
    hl.setTheme(m_repo->defaultTheme(dark ? Repository::DarkTheme : Repository::LightTheme));

    std::vector<QString> out;
    out.reserve(lines.size());
    State state;
    for (const QString& ln : lines)
    {
        hl.beginLine(ln);
        state = hl.highlight(ln, state);
        out.push_back(hl.endLine());
    }
    return out;
}

} // namespace gittide::ui
