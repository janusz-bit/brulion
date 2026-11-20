// Reading the cells of view rows: page and block properties as written (typed for editing),
// built-in fields, Dataview values of formulas, covers. Shared by query views and bases. Internal
// to the views module.
#pragma once

#include "views/view.h"

#include "dataview/expression.h"
#include "markdown/pageprops.h"

#include <QHash>

#include <memory>

namespace brulion::views::detail {

/// A Dataview value as a cell (Markdown via dataview::toMarkdown(), an editor value).
Cell valueCell(const dataview::Value &value, const VaultIndex &index);
/// A page property's cell (markdown::readPageProperties()'s item).
Cell pagePropertyCell(const markdown::PageProperty &property);
/// "text", "number", … of a PageProperty's type; "" for unsupported values.
QString editType(markdown::PropertyType type);

class CellReader {
public:
    CellReader(const VaultIndex &index, const ViewOptions &options);
    ~CellReader();

    const VaultIndex &index() const { return m_index; }
    /// The page properties of a note, read once.
    const markdown::PageProperties &pageProperties(const VaultPath &path);
    /// The page property `folded` (queryPropertyKey()) of a note, or nullptr.
    const markdown::PageProperty *pageProperty(const VaultPath &path, const QString &folded);
    /// A property cell of a row: a page row's page property; a block row's own `key::` line, else
    /// its nearest ancestor's, else its page's (edits go to the block's own line: target "block").
    Cell propertyCell(const Row &row, const QString &folded);
    /// A built-in column's cell.
    Cell builtinCell(const Row &row, ColumnKind kind);
    /// The cover of a row: a note's `banner`, else the first picture embedded in it (in the block, for
    /// a block row). Sets row.cover and row.coverKind.
    void cover(Row &row);
    /// A cover from a property value as written (Bases' `image:`): a link to a picture, a URL, or a
    /// colour "#rrggbb".
    void coverFrom(Row &row, const QString &written);
    /// The evaluator of the run (made when first asked: rows without formulas need none).
    dataview::Evaluator &evaluator();
    std::optional<FileStat> stat(const VaultPath &path);

private:
    const VaultIndex &m_index;
    const ViewOptions &m_options;
    QHash<VaultPath, markdown::PageProperties> m_pages;
    QHash<VaultPath, std::optional<FileStat>> m_stats;
    std::unique_ptr<dataview::Evaluator> m_evaluator;
};

/// A task status / priority by its name ("todo", "doing", … / "highest" … "none").
std::optional<TaskStatus> statusNamed(const QString &name);
QString statusName(TaskStatus status);

} // namespace brulion::views::detail
