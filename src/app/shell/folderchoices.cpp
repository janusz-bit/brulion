#include "folderchoices.h"

#include "texthelper.h"

#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QVariantList folderChoices(const QStringList &allFolders, const QString &query, const QString &ownFolder,
                           const QString &currentFolder, bool moving, const FolderChoiceTexts &texts)
{
    QStringList folders;
    for (const QString &f : allFolders) {
        if (ownFolder.isEmpty() || (f != ownFolder && !f.startsWith(ownFolder + u'/')))
            folders.append(f);
    }
    const QString q = query.trimmed();
    const ui::TextHelper helper;
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
    if (q.isEmpty() || helper.fold(texts.rootName).contains(helper.fold(q)))
        row(QString(), texts.rootName, QString(), u"house"_s, false);
    QStringList names;
    names.reserve(folders.size());
    for (const QString &f : std::as_const(folders))
        names.append(f.mid(f.lastIndexOf(u'/') + 1));
    const QVariantList ranked = helper.fuzzyRank(q, names, 100, folders);
    for (const QVariant &r : ranked) {
        const int index = r.toMap().value(u"index"_s).toInt();
        const QString &f = folders.at(index);
        const qsizetype slash = f.lastIndexOf(u'/');
        row(f, names.at(index), slash >= 0 ? f.left(slash) : QString(), u"folder"_s, false);
    }
    static const QRegularExpression edgeSlashes(u"^/+|/+$"_s);
    const QString typed = QString(q).remove(edgeSlashes);
    if (!typed.isEmpty()) {
        const QString lower = typed.toLower();
        const bool exists =
                std::any_of(folders.cbegin(), folders.cend(), [&](const QString &f) { return f.toLower() == lower; });
        if (!exists)
            row(typed, QString(texts.newFolder).arg(typed), texts.createSubtitle, u"folder-plus"_s, true);
    }
    return out;
}

} // namespace brulion::shell
