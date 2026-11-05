// JsHelper — the JavaScript built-ins that qmlcachegen (Qt 6.11) cannot compile to C++, with the
// same results, for QML compiled ahead of time (docs/UI.md "Compiled QML"): string methods
// (`s.trim()` → `Js.trim(s)`), number parsing and formatting, regular expressions, building lists
// (`push`, `concat`, `sort` do not compile on QML lists) and JSON.stringify.
// QML singleton `Js` in module Brulion (registered by qmlforeign.h; the class itself is QML-free,
// library brulion_ui_helpers). tests/app/tst_jshelper.cpp checks every function against the
// JavaScript engine's own result.
//
// Strings are UTF-16 like in JavaScript: indices and lengths count UTF-16 code units. No default
// arguments (each would be an overload for QML): a variant with more arguments has its own name.
#pragma once

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::ui {

class JsHelper : public QObject {
    Q_OBJECT

public:
    explicit JsHelper(QObject *parent = nullptr);

    // --- String.prototype ---
    /// s.indexOf(search)
    Q_INVOKABLE int indexOf(const QString &s, const QString &search) const;
    /// s.indexOf(search, from)
    Q_INVOKABLE int indexOfFrom(const QString &s, const QString &search, int from) const;
    /// s.lastIndexOf(search)
    Q_INVOKABLE int lastIndexOf(const QString &s, const QString &search) const;
    /// s.includes(search)
    Q_INVOKABLE bool includes(const QString &s, const QString &search) const;
    /// s.startsWith(prefix)
    Q_INVOKABLE bool startsWith(const QString &s, const QString &prefix) const;
    /// s.startsWith(prefix, position)
    Q_INVOKABLE bool startsWithAt(const QString &s, const QString &prefix, int position) const;
    /// s.endsWith(suffix)
    Q_INVOKABLE bool endsWith(const QString &s, const QString &suffix) const;
    /// s.slice(start) — a negative start counts from the end.
    Q_INVOKABLE QString sliceFrom(const QString &s, int start) const;
    /// s.slice(start, end) — negative indices count from the end.
    Q_INVOKABLE QString slice(const QString &s, int start, int end) const;
    /// s.substring(start, end) — clamped to [0, length], swapped when start > end.
    Q_INVOKABLE QString substring(const QString &s, int start, int end) const;
    /// s.substring(start)
    Q_INVOKABLE QString substringFrom(const QString &s, int start) const;
    /// s.charAt(i) ("" out of range)
    Q_INVOKABLE QString charAt(const QString &s, int i) const;
    /// s.charCodeAt(i) (NaN out of range)
    Q_INVOKABLE double charCodeAt(const QString &s, int i) const;
    /// s.toLowerCase()
    Q_INVOKABLE QString toLowerCase(const QString &s) const;
    /// s.toUpperCase()
    Q_INVOKABLE QString toUpperCase(const QString &s) const;
    /// s.trim() — JavaScript's white space and line terminators.
    Q_INVOKABLE QString trim(const QString &s) const;
    /// s.trimStart()
    Q_INVOKABLE QString trimStart(const QString &s) const;
    /// s.trimEnd()
    Q_INVOKABLE QString trimEnd(const QString &s) const;
    /// s.split(separator) — "" splits into UTF-16 code units; empty parts are kept.
    Q_INVOKABLE QStringList split(const QString &s, const QString &separator) const;
    /// s.replace(search, replacement) with a string `search`: the first occurrence, `replacement`
    /// taken literally (no `$&` patterns).
    Q_INVOKABLE QString replace(const QString &s, const QString &search, const QString &replacement) const;
    /// s.replaceAll(search, replacement) with a string `search`, `replacement` taken literally.
    Q_INVOKABLE QString replaceAll(const QString &s, const QString &search, const QString &replacement) const;
    /// s.repeat(count) ("" for count <= 0)
    Q_INVOKABLE QString repeat(const QString &s, int count) const;
    /// s.padStart(length, fill)
    Q_INVOKABLE QString padStart(const QString &s, int length, const QString &fill) const;
    /// s.padEnd(length, fill)
    Q_INVOKABLE QString padEnd(const QString &s, int length, const QString &fill) const;
    /// encodeURIComponent(s): UTF-8 percent-encoding of all but A-Z a-z 0-9 - _ . ! ~ * ' ( ).
    Q_INVOKABLE QString encodeURIComponent(const QString &s) const;
    /// decodeURIComponent(s): every %XX decoded as UTF-8. Malformed escapes stay as they are
    /// (JavaScript throws a URIError).
    Q_INVOKABLE QString decodeURIComponent(const QString &s) const;

    // --- Numbers ---
    /// Number(s): trimmed decimal (or 0x/0o/0b) literal, "" → 0, "Infinity", otherwise NaN.
    Q_INVOKABLE double toNumber(const QString &s) const;
    /// parseInt(s, 10): the leading decimal integer, NaN when there is none.
    Q_INVOKABLE double parseInt(const QString &s) const;
    /// parseFloat(s): the leading decimal number, NaN when there is none.
    Q_INVOKABLE double parseFloat(const QString &s) const;
    /// isNaN(x)
    Q_INVOKABLE bool isNaN(double x) const;
    /// isFinite(x)
    Q_INVOKABLE bool isFinite(double x) const;
    /// x.toFixed(digits) (0..100 digits; "NaN", "Infinity" like JavaScript)
    Q_INVOKABLE QString toFixed(double x, int digits) const;
    /// String(x) for a number: "3", "0.5", "1e+21", "NaN" — what JavaScript prints.
    Q_INVOKABLE QString numberToString(double x) const;
    /// Math.atan2(y, x) (Math.atan2 is not compiled; Math's other functions are).
    Q_INVOKABLE double atan2(double y, double x) const;

    // --- Regular expressions: `pattern` and `flags` as in a JavaScript literal /pattern/flags
    // (flags i, m, s, g; others ignored; PCRE syntax beyond JavaScript's is not refused). Patterns
    // are compiled once and cached; an invalid one matches nothing. ---
    /// /pattern/flags.test(s)
    Q_INVOKABLE bool test(const QString &s, const QString &pattern, const QString &flags) const;
    /// s.search(/pattern/flags): index of the first match, -1 without one.
    Q_INVOKABLE int search(const QString &s, const QString &pattern, const QString &flags) const;
    /// s.match(/pattern/flags) without the g flag: [match, group 1, …] (a group that did not
    /// take part is ""), [] without a match. With g: every match.
    Q_INVOKABLE QStringList match(const QString &s, const QString &pattern, const QString &flags) const;
    /// s.replace(/pattern/flags, replacement): the first match, every one with g; `replacement`
    /// with JavaScript's patterns $$, $&, $`, $', $1…$99 (like the QML engine, not $<name>).
    Q_INVOKABLE QString replaceRegExp(const QString &s, const QString &pattern, const QString &flags,
                                      const QString &replacement) const;
    /// s.split(/pattern/flags) (capture groups are not inserted)
    Q_INVOKABLE QStringList splitRegExp(const QString &s, const QString &pattern, const QString &flags) const;

    // --- Lists (results are new lists; the arguments stay unchanged) ---
    /// [...list, value]
    Q_INVOKABLE QVariantList appended(const QVariantList &list, const QVariant &value) const;
    /// list.concat(other)
    Q_INVOKABLE QVariantList concat(const QVariantList &list, const QVariantList &other) const;
    /// list without the element at `index` (unchanged out of range) — list.splice(index, 1).
    Q_INVOKABLE QVariantList removedAt(const QVariantList &list, int index) const;
    /// list with `value` inserted at `index` (clamped) — list.splice(index, 0, value).
    Q_INVOKABLE QVariantList insertedAt(const QVariantList &list, int index, const QVariant &value) const;
    /// list.slice().reverse()
    Q_INVOKABLE QVariantList reversed(const QVariantList &list) const;
    /// [...list, value] for strings
    Q_INVOKABLE QStringList appendedString(const QStringList &list, const QString &value) const;
    /// list.concat(other) for strings
    Q_INVOKABLE QStringList concatStrings(const QStringList &list, const QStringList &other) const;
    /// list without the string at `index` (unchanged out of range)
    Q_INVOKABLE QStringList removedStringAt(const QStringList &list, int index) const;
    /// list with `value` inserted at `index` (clamped)
    Q_INVOKABLE QStringList insertedStringAt(const QStringList &list, int index, const QString &value) const;
    /// list without every occurrence of `value`
    Q_INVOKABLE QStringList withoutString(const QStringList &list, const QString &value) const;
    /// list.slice().sort() — JavaScript's default order (UTF-16 code units).
    Q_INVOKABLE QStringList sortedStrings(const QStringList &list) const;
    /// The strings in their first order without repetitions.
    Q_INVOKABLE QStringList uniqueStrings(const QStringList &list) const;
    /// list.slice().reverse() for strings
    Q_INVOKABLE QStringList reversedStrings(const QStringList &list) const;
    /// The non-empty strings of `list` (list.filter(s => s !== "")).
    Q_INVOKABLE QStringList nonEmptyStrings(const QStringList &list) const;
    /// Each string trimmed (list.map(s => s.trim())).
    Q_INVOKABLE QStringList trimmedStrings(const QStringList &list) const;
    /// A list of `count` copies of `value` (Array(count).fill(value)).
    Q_INVOKABLE QVariantList filled(int count, const QVariant &value) const;
    /// [0, 1, …, count − 1]
    Q_INVOKABLE QList<int> range(int count) const;

    // --- Objects ---
    /// Object.keys(map) — in the map's order (sorted, as QVariantMap keeps its keys).
    Q_INVOKABLE QStringList keys(const QVariantMap &map) const;
    /// Object.assign({}, map, changes): a copy of `map` with the keys of `changes` set.
    Q_INVOKABLE QVariantMap merged(const QVariantMap &map, const QVariantMap &changes) const;
    /// A copy of `map` with `key` set to `value` ({ ...map, [key]: value }).
    Q_INVOKABLE QVariantMap withKey(const QVariantMap &map, const QString &key, const QVariant &value) const;
    /// A copy of `map` without `key`.
    Q_INVOKABLE QVariantMap withoutKey(const QVariantMap &map, const QString &key) const;
    /// key in map
    Q_INVOKABLE bool hasKey(const QVariantMap &map, const QString &key) const;

    /// JSON.stringify(value) for maps, lists, strings, numbers, booleans and null (keys sorted,
    /// as in a QVariantMap); "" for undefined.
    Q_INVOKABLE QString stringify(const QVariant &value) const;

private:
    const QRegularExpression &regExp(const QString &pattern, const QString &flags) const;

    mutable QHash<QString, QRegularExpression> m_regExps;
};

} // namespace brulion::ui
