#pragma once
#include <memory>
#include <vector>

#include <QString>

namespace KSyntaxHighlighting
{
class Repository;
}

namespace gittide::ui
{

/// Lexical syntax highlighter over KSyntaxHighlighting. Owns a Repository (the
/// expensive-to-build definition/theme store) and produces per-line HTML for a
/// stateful stream of lines. UI-only; no core dependency. See Plan 30 / D45.
class SyntaxHighlighter
{
public:
    SyntaxHighlighter();
    ~SyntaxHighlighter();

    bool hasDefinition(const QString& filePath) const;

    std::vector<QString> highlightLines(const QString& filePath,
                                        const std::vector<QString>& lines,
                                        bool dark) const;

private:
    std::unique_ptr<KSyntaxHighlighting::Repository> m_repo;
};

} // namespace gittide::ui
