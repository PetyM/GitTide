#pragma once
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/filestatus.hpp"

namespace gittide::ui {

/// QML list model of changed files. One row per FileStatus. Carries a whole-file
/// tri-state check (Unchecked/Partial/Checked) used to build StageSelections at
/// commit time. Knows nothing about themes: it exposes a status letter
/// ("A"/"M"/"D"/"U"/"?") and a status kind ("added"/"modified"/"deleted"/
/// "untracked") and lets the QML delegate pick the colour token.
class ChangedFilesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Check
    {
        Unchecked = 0,
        Partial   = 1,
        Checked   = 2,
    };
    Q_ENUM(Check)

    enum Roles
    {
        DirRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        LetterRole,
        KindRole,
        CheckRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFiles(const std::vector<gittide::FileStatus>& files);

    Q_INVOKABLE void setChecked(int row, bool checked);
    void setCheckState(int row, Check state);
    Check checkState(int row) const;
    QString pathAt(int row) const;
    int rowForPath(const QString& path) const;
    int checkedCount() const;

    static QString letterForFlags(gittide::StatusFlag flags);
    static QString kindForFlags(gittide::StatusFlag flags);

private:
    struct Row
    {
        QString dir;
        QString name;
        QString path;
        QString letter;
        QString kind;
        Check   check = Checked;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
