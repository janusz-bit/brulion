#include "folderchoices.h"

#include "texthelper.h"

#include "text/text.h"

#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QVariantList folderChoices(const QStringList &allFolders, const QString &query, const QString &ownFolder,
                           const QString &currentFolder, bool moving, const FolderChoiceTexts &texts)
{
    QStringList folders = allFolders;
    if (!ownFolder.isEmpty()) {
        const QString inOwn = ownFolder + u'/';
        folders.removeIf([&](const QString &f) { return f == ownFolder || f.startsWith(inOwn); });
    }
    const QString q = query.trimmed();
    QVariantList out;
    const auto row = [&](const QString &folder, const QString &title, const QString &subtitle, const QString &icon,
                         bool create) {
        out.append(QVariantMap{{u"folder"_s, folder},
                               {u"title"_s, title},
                               {u"subtitle"_s, subtitle},
                               {u"icon"_s, icon},
                               {u"create"_s, create},
                               {u"disabled"_s, moving && !create && folder == currentFolder}});
    };
    if (q.isEmpty() || text::foldKey(texts.rootName).contains(text::foldKey(q)))
        row(QString(), texts.rootName, QString(), u"house"_s, false);
    QStringList names;
    names.reserve(folders.size());
    for (const QString &f : std::as_const(folders))
        names.append(f.mid(f.lastIndexOf(u'/') + 1));
    // TextHelper::fuzzyRank() of the names, then the paths.
    for (const QList<ui::FuzzyRanked> ranked = ui::fuzzyRankList(q, names, 100, folders); const auto &r : ranked) {
        const QString &f = folders.at(r.index);
        const qsizetype slash = f.lastIndexOf(u'/');
        row(f, names.at(r.index), slash >= 0 ? f.left(slash) : QString(), u"folder"_s, false);
    }
    static const QRegularExpression edgeSlashes(u"^/+|/+$"_s);
    const QString typed = QString(q).remove(edgeSlashes);
    if (!typed.isEmpty()) {
        const QString lower = typed.toLower();
        if (std::ranges::none_of(std::as_const(folders), [&](const QString &f) { return f.toLower() == lower; }))
            row(typed, texts.newFolder.arg(typed), texts.createSubtitle, u"folder-plus"_s, true);
    }
    return out;
}

} // namespace brulion::shell
