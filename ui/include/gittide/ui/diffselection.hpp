#pragma once
#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>
#include <QString>

namespace gittide::ui {

/// Character-accurate text selection over a diff list model (DiffLinesModel).
///
/// The selection is two (row, column) positions in *model* coordinates: row is a
/// model index, column a character offset into that row's `lineText`. It lives
/// here rather than in the QML delegates because a ListView destroys off-screen
/// delegates, and a selection routinely spans rows that no longer exist as items.
///
/// The anchor is where the drag started and the cursor where it currently is;
/// either order is valid and every query normalises them, so a bottom-up or
/// right-to-left drag yields the same result as the forward one.
///
/// Reads the model through the role names `lineText` and `lineKind`, so it works
/// with any model that publishes them.
class DiffSelection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int anchorRow READ anchorRow NOTIFY selectionChanged)
public:
    explicit DiffSelection(QObject* parent = nullptr);

    QAbstractItemModel* model() const;

    /// Attach to @p model. Clears any selection and re-subscribes the clear-on-
    /// reset connections, so a file switch never leaves a stale selection behind.
    void setModel(QAbstractItemModel* model);

    /// True when anchor and cursor differ — a bare click selects nothing.
    bool hasSelection() const;

    /// Row the current drag started on; -1 when there is no anchor. QML reads it
    /// to decide which end of a row to extend to when a row has no code item.
    int anchorRow() const;

    /// Start a selection at (@p row, @p col): anchor and cursor both land there.
    Q_INVOKABLE void begin(int row, int col);

    /// Move the cursor to (@p row, @p col), keeping the anchor. Falls back to
    /// begin() when there is no anchor yet (a Shift+click with nothing selected).
    Q_INVOKABLE void extendTo(int row, int col);

    /// Select every row, from column 0 to the end of the last row. No-op on an
    /// empty model.
    Q_INVOKABLE void selectAll();

    /// Drop the selection.
    Q_INVOKABLE void clear();

    /// Select the word around (@p row, @p col) — a run of letters, digits and
    /// underscores. On a separator character selects that single character; on an
    /// empty row selects nothing. @p col past the row end is treated as the last
    /// character, so a double-click in the blank area past a line takes its last
    /// word.
    Q_INVOKABLE void selectWord(int row, int col);

    /// Select all of @p row. Selects nothing on an empty row.
    Q_INVOKABLE void selectLine(int row);

    /// First selected column in @p row, or -1 when the row is outside the
    /// selection. Clamped to the row's length.
    Q_INVOKABLE int startInRow(int row) const;

    /// One past the last selected column in @p row, or -1 when the row is outside
    /// the selection. Clamped to the row's length.
    Q_INVOKABLE int endInRow(int row) const;

    /// Character count of @p row's text; 0 for an unknown row.
    Q_INVOKABLE int rowLength(int row) const;

    /// The selected text. With @p withMarkers each row is prefixed "+ ", "- " or
    /// "  " by its kind (ASCII hyphen, not the "−" the view draws), so the result
    /// reads as a patch fragment; hunk headers are always copied verbatim and
    /// unprefixed. Synthetic "block" rows contribute nothing. Rows are joined
    /// with "\n" and a trailing "\n" is appended. Empty without a selection.
    Q_INVOKABLE QString copyText(bool withMarkers = false) const;

signals:
    void modelChanged();
    void selectionChanged();

private:
    struct Pos
    {
        int row = -1;
        int col = 0;
    };

    /// True if cursor comes before anchor in document order.
    bool    cursorIsFirst() const;
    Pos     orderedStart() const;
    Pos     orderedEnd() const;

    /// Fetch data for a row by role name, handling model/bounds checks.
    QVariant dataFor(int row, const QByteArray& roleName) const;

    QString rowText(int row) const;
    QString rowKind(int row) const;
    int     roleOf(const QByteArray& name) const;

    QPointer<QAbstractItemModel> m_model;
    Pos                          m_anchor;
    Pos                          m_cursor;
};

} // namespace gittide::ui
