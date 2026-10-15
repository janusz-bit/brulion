// Logseq syntax helpers shared by the Logseq / Roam importers and the Obsidian / Logseq
// exporters: block conversion (markers, priorities, planning, id::), page properties, macros,
// journal page titles ("Oct 14th, 2026"), namespace file names and deterministic UUIDs.
// Internal to the io module.
#pragma once

#include "types.h"

#include <QDate>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace brulion::io::detail {

/// Parses journal page titles written with a date-fns format ("MMM do, yyyy" — Logseq's
/// default; "MMMM do, yyyy" — Roam; "yyyy-MM-dd", "EEE, dd.MM.yyyy", …).
class JournalTitleFormat {
public:
    explicit JournalTitleFormat(const QString &dateFnsFormat = QStringLiteral("MMM do, yyyy"));
    /// The date of a title written in this format (whole string, case-insensitive), or nullopt.
    std::optional<QDate> parse(const QString &title) const;
    /// The title of `date` in this format (English names).
    QString format(QDate date) const;

private:
    enum class Token { Year4, Year2, MonthLong, MonthShort, Month2, Month1, DayOrdinal, Day2, Day1, Weekday, Literal };
    QList<std::pair<Token, QString>> m_tokens;
    QRegularExpression m_regex;
};

/// Journal date of a Logseq / Roam title in any of `formats` (or ISO yyyy-MM-dd).
std::optional<QDate> parseJournalTitle(const QString &title, const QList<JournalTitleFormat> &formats);

/// Leading page properties of a Logseq page (`key:: value` lines before the first block, or a
/// first bullet consisting only of properties). Keys lower-cased in document order.
struct LogseqPageProperties {
    QVariantMap values; ///< key -> raw value (trimmed)
    QStringList keys;
    QString body; ///< the content without them
};
LogseqPageProperties takeLogseqPageProperties(const QString &content);

/// Convert Logseq block syntax to Brulion's (content in -> content out, untouched lines kept):
/// TODO/LATER -> `[ ]`, DOING/NOW/IN-PROGRESS -> `[/]`, DONE -> `[x]`, CANCELED/CANCELLED ->
/// `[-]`, WAITING/WAIT -> `[>]`; `[#A]`/`[#B]`/`[#C]` on tasks -> ⏫/🔼/🔽; SCHEDULED: / DEADLINE:
/// lines -> ⏳ / 📅 (+ 🔁 from a repeater) on the first line; `id::` -> ` ^id`; `collapsed::`
/// lines and :LOGBOOK: drawers removed; `{{embed [[p]]}}` -> `![[p]]`, `{{embed ((id))}}` ->
/// `!((id))`, `[alias]([[p]])` -> `[[p|alias]]`. Code (fences, inline code) is never changed.
QString logseqBlocksToBrulion(const QString &content);

/// Logseq page name of a file name: "a___b" -> "a/b" (namespaces), legacy "a%2Fb" -> "a/b",
/// other %XX sequences decoded, ".md" removed.
QString logseqPageNameFromFile(const QString &fileName);

/// Logseq file name (without extension) of a page name: "a/b" -> "a___b"; characters that
/// file systems reject (`<>:"|?*\`, control characters, a leading '.') are %XX-encoded.
QString logseqFileNameForPage(const QString &pageName);

/// A deterministic UUID (version-4 layout) derived from `seed` (SHA-1): the same id always maps
/// to the same UUID, so references survive an export.
QString uuidFromSeed(const QString &seed);

/// "<2026-10-15 Thu 10:00 .+1w>" — a Logseq timestamp for a date(-time) and repeater.
QString logseqTimestamp(const DateSpec &spec, const QString &repeater = {});

} // namespace brulion::io::detail
