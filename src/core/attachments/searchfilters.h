// The whole-notebook search field's filters (docs/UI.md "Search"): words of the form `in:value`
// narrow the results to kinds of files, the other words are searched. Part of the attachments
// module — see attachments/attachments.h.
//
//   in:notes  in:pages  in:journals  in:attachments  in:pdf  in:pictures
//
// The key may also be `type:`, or the Polish `w:` / `typ:`; the values in English or Polish
// (notes/notatki, pages/strony, journals/dziennik, attachments/załączniki/files/pliki, pdf,
// pictures/images/photos/obrazy/zdjęcia — singular forms too), case and diacritics optional
// ("w:zalaczniki"). Several filters add up ("in:pdf in:pictures"); a word that only looks like
// one ("in:xyz", "http://…", "12:30") stays a word of the query.
#pragma once

#include <QFlags>
#include <QString>
#include <QStringView>

namespace brulion::attachments {

enum class Scope : quint8 {
    /// Notes that are not daily notes.
    Pages = 1,
    /// Daily notes.
    Journals = 2,
    /// PDFs (their text layer and OCR of their scanned pages).
    Pdf = 4,
    /// Pictures (OCR).
    Pictures = 8,
};
Q_DECLARE_FLAGS(Scopes, Scope)
Q_DECLARE_OPERATORS_FOR_FLAGS(Scopes)

inline constexpr Scopes AllScopes = Scopes(Scope::Pages) | Scope::Journals | Scope::Pdf | Scope::Pictures;
inline constexpr Scopes NoteScopes = Scopes(Scope::Pages) | Scope::Journals;
inline constexpr Scopes AttachmentScopes = Scopes(Scope::Pdf) | Scope::Pictures;

struct SearchFilters {
    /// The query without its filters (the other words in their order, single spaces).
    QString text;
    /// What the filters let through (AllScopes without any).
    Scopes scopes = AllScopes;
    /// At least one filter was written.
    bool filtered = false;
};

SearchFilters parseSearchFilters(QStringView query);

} // namespace brulion::attachments
