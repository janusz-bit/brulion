#include "attachments/searchfilters.h"

#include "text/text.h"

#include <QStringList>

#include <array>
#include <optional>

namespace brulion::attachments {

using namespace Qt::StringLiterals;

namespace {

struct Value {
    QStringView name; ///< folded
    Scopes scopes;
};

const std::array<Value, 36> &values()
{
    static const std::array<Value, 36> table{{
            {u"notes", NoteScopes},
            {u"note", NoteScopes},
            {u"notatki", NoteScopes},
            {u"notatka", NoteScopes},
            {u"pages", Scope::Pages},
            {u"page", Scope::Pages},
            {u"strony", Scope::Pages},
            {u"strona", Scope::Pages},
            {u"journals", Scope::Journals},
            {u"journal", Scope::Journals},
            {u"dziennik", Scope::Journals},
            {u"dzienniki", Scope::Journals},
            {u"attachments", AttachmentScopes},
            {u"attachment", AttachmentScopes},
            {u"files", AttachmentScopes},
            {u"file", AttachmentScopes},
            {u"zalaczniki", AttachmentScopes},
            {u"zalacznik", AttachmentScopes},
            {u"pliki", AttachmentScopes},
            {u"plik", AttachmentScopes},
            {u"pdf", Scope::Pdf},
            {u"pdfs", Scope::Pdf},
            {u"pictures", Scope::Pictures},
            {u"picture", Scope::Pictures},
            {u"images", Scope::Pictures},
            {u"image", Scope::Pictures},
            {u"photos", Scope::Pictures},
            {u"obrazy", Scope::Pictures},
            {u"obraz", Scope::Pictures},
            {u"zdjecia", Scope::Pictures},
            {u"documents", Scope::Documents},
            {u"document", Scope::Documents},
            {u"docs", Scope::Documents},
            {u"doc", Scope::Documents},
            {u"dokumenty", Scope::Documents},
            {u"dokument", Scope::Documents},
    }};
    return table;
}

/// The scopes a word names when it is a filter.
std::optional<Scopes> filterOf(QStringView word)
{
    const qsizetype colon = word.indexOf(u':');
    if (colon <= 0 || colon == word.size() - 1)
        return std::nullopt;
    const QString key = text::foldKey(word.first(colon));
    if (key != u"in"_s && key != u"type"_s && key != u"w"_s && key != u"typ"_s)
        return std::nullopt;
    const QString value = text::foldKey(word.sliced(colon + 1));
    if (value == u"zdjecie"_s)
        return Scopes(Scope::Pictures);
    for (const Value &v : values()) {
        if (v.name == value)
            return v.scopes;
    }
    return std::nullopt;
}

} // namespace

SearchFilters parseSearchFilters(QStringView query)
{
    SearchFilters out;
    Scopes named;
    QStringList words;
    for (const QStringView word : query.tokenize(u' ', Qt::SkipEmptyParts)) {
        const QStringView trimmed = word.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (const std::optional<Scopes> scopes = filterOf(trimmed)) {
            named |= *scopes;
            out.filtered = true;
            continue;
        }
        words.append(trimmed.toString());
    }
    out.text = words.join(u' ');
    out.scopes = out.filtered ? named : AllScopes;
    return out;
}

} // namespace brulion::attachments
