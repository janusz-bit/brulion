// uilayout — the interface's layout as text (docs/NOTEBOOK-SETTINGS.md "The layout language"): calls
// in the style of Typst, one a place, the items in order, `|` a group's end, `-id` an item hidden.
//
//   #rail(journals, tasks, |, calendar)
//   #panel(favorites, recent, -pages)
//   #bottom(journals, tasks, search, calendar, allPages)
//
// Pure: no evaluation — the places are a fixed set, the arguments names. What does not read is left
// out with a warning; the rest applies.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::uilayout {

/// One place's items as written: names, "|" for a group's end, "-name" for one hidden.
struct Place {
    QString name;
    QStringList items;
    bool operator==(const Place &) const = default;
};

struct Layout {
    /// The places written, in order (each once: a second one of a name is left out).
    QList<Place> places;
    /// What was left out and why (English, one a problem, with its line number).
    QStringList warnings;
    /// The place `name` (nullptr: not written — the app's own layout there).
    const Place *place(QStringView name) const;
};

/// What each place may hold: its items' names (a place not in `known` is unknown).
using Vocabulary = QHash<QString, QStringList>;

/// The places and their items: rail, panel, bottom (at most MaxBottom items).
const Vocabulary &defaultVocabulary();
inline constexpr int MaxBottom = 5;

/// `text` read with `vocabulary`'s places and items. Lines that are no call (blank, `//` comments,
/// anything else: a warning) are skipped; an unknown place, an unknown item, an item written twice,
/// a sixth bottom button are left out with a warning.
Layout parse(QStringView text, const Vocabulary &vocabulary = defaultVocabulary());

/// The text of `layout` (one call a line, items joined by ", "): parse(print(l)) gives l's places.
QString print(const Layout &layout);

/// The text of the first ```brulion-layout block of a note ("" none).
QString blockOf(QStringView note);

/// The items of `place` shown, in order, without groups and hidden ones; `fallback` when the layout
/// does not write the place.
QStringList shown(const Layout &layout, QStringView place, const QStringList &fallback);

} // namespace brulion::uilayout
