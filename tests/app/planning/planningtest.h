// Shared helpers of the planning tests: an in-memory VaultIndex from (path, content) pairs.
#pragma once

#include "index/vaultindex.h"

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <utility>

namespace brulion::planning::test {

using Files = QList<std::pair<QString, QString>>;

inline std::unique_ptr<VaultIndex> makeIndex(const Files &files, const VaultConfig &config = VaultConfig())
{
    auto index = std::make_unique<VaultIndex>(config);
    {
        VaultIndex::Batch batch(*index);
        for (const auto &[path, content] : files)
            index->update(path, content);
    }
    return index;
}

/// What a view shows of a model when it reads rows only when the model's signals say so: every
/// row's text (`rowText`) taken when it is inserted, signalled by dataChanged or after a reset,
/// and moved and removed with the model's rows. Equal to the model's rows read afresh only when
/// the model signals every row whose data changed.
class ModelMirror : public QObject {
public:
    ModelMirror(QAbstractItemModel *model, std::function<QString(int row)> rowText)
        : m_model(model), m_text(std::move(rowText))
    {
        reset();
        connect(model, &QAbstractItemModel::modelReset, this, &ModelMirror::reset);
        connect(model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first, int last) {
            for (int r = first; r <= last; ++r)
                rows.insert(r, m_text(r));
        });
        connect(model, &QAbstractItemModel::rowsRemoved, this,
                [this](const QModelIndex &, int first, int last) { rows.remove(first, last - first + 1); });
        connect(model, &QAbstractItemModel::rowsMoved, this,
                [this](const QModelIndex &, int first, int last, const QModelIndex &, int dest) {
                    const QStringList block = rows.mid(first, last - first + 1);
                    rows.remove(first, block.size());
                    const qsizetype at = dest > last ? dest - block.size() : dest;
                    for (qsizetype k = 0; k < block.size(); ++k)
                        rows.insert(at + k, block.at(k));
                });
        connect(model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &tl, const QModelIndex &br) {
            for (int r = tl.row(); r <= br.row(); ++r) {
                rows[r] = m_text(r);
                ++changedRows;
            }
        });
    }
    /// The model's rows read now.
    QStringList current() const
    {
        QStringList out;
        for (int r = 0; r < m_model->rowCount(); ++r)
            out << m_text(r);
        return out;
    }

    QStringList rows;
    int changedRows = 0; ///< rows signalled by dataChanged

private:
    void reset() { rows = current(); }

    QAbstractItemModel *m_model;
    std::function<QString(int)> m_text;
};

} // namespace brulion::planning::test
