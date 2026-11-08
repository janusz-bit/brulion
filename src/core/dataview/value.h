// The values of Dataview expressions (docs/SYNTAX.md §10 "Dataview queries"): null, booleans,
// numbers, strings, dates, durations, links, lists, objects and functions (lambdas), with
// Dataview's truthiness, ordering and equality. Part of the dataview module — see
// dataview/dataview.h.
#pragma once

#include "types.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace brulion::dataview {

/// A date with or without a time of day (local wall-clock time, never converted through UTC).
struct DateValue {
    QDateTime at; ///< local; 00:00 when !hasTime
    bool hasTime = false;

    friend bool operator==(const DateValue &, const DateValue &) = default;
};

/// A length of time: months (months and years) and milliseconds (weeks and shorter units).
struct Duration {
    qint64 months = 0;
    qint64 ms = 0;

    friend bool operator==(const Duration &, const Duration &) = default;
};

/// A link to a page (or a part of it).
struct LinkValue {
    /// The vault path of the file when it exists (resolved), else the target as written.
    QString path;
    QString display; ///< `|display`, "" for none
    QString subpath; ///< heading, or `^id` of a block; "" for the whole page
    bool embed = false;
    bool resolved = false;

    friend bool operator==(const LinkValue &, const LinkValue &) = default;
};

class Value;
using ValueList = QList<Value>;

/// An object's fields, looked up by name. Page, file and task objects compute their fields when
/// asked (dataview/objects.cpp); a map object holds them.
class Object {
public:
    virtual ~Object() = default;
    /// The field `key` (a canonical key: lower-case, blanks as '-'), or nullopt.
    virtual std::optional<Value> field(const QString &key) const = 0;
    /// The field names, in the order they are shown (object values rendered as text, `extract`).
    virtual QStringList keys() const = 0;
};

struct Function; // a lambda (eval_p.h)

class Value {
public:
    enum class Type { Null, Boolean, Number, String, Date, Duration, Link, List, Object, Function };

    Value() = default;
    static Value boolean(bool b);
    static Value number(double n);
    static Value string(const QString &s);
    static Value date(const DateValue &d);
    static Value date(QDate d);
    static Value duration(const Duration &d);
    static Value link(const LinkValue &l);
    static Value list(ValueList items);
    static Value object(std::shared_ptr<const Object> o);
    /// A map object (keys canonical, shown in the order given).
    static Value map(const QList<std::pair<QString, Value>> &fields);
    static Value function(std::shared_ptr<const Function> f);

    Type type() const { return Type(m.index()); }
    bool isNull() const { return m.index() == 0; }
    bool is(Type t) const { return type() == t; }

    bool asBool() const { return std::get<bool>(m); }
    double asNumber() const { return std::get<double>(m); }
    const QString &asString() const { return std::get<QString>(m); }
    const DateValue &asDate() const { return std::get<DateValue>(m); }
    const Duration &asDuration() const { return std::get<Duration>(m); }
    const LinkValue &asLink() const { return std::get<LinkValue>(m); }
    const ValueList &asList() const;
    const Object &asObject() const { return *std::get<std::shared_ptr<const Object>>(m); }
    std::shared_ptr<const Object> objectPtr() const { return std::get<std::shared_ptr<const Object>>(m); }
    const Function &asFunction() const { return *std::get<std::shared_ptr<const Function>>(m); }
    std::shared_ptr<const Function> functionPtr() const { return std::get<std::shared_ptr<const Function>>(m); }

    /// Dataview's truthiness: null, false, 0, "", an empty list or object are false.
    bool truthy() const;

    /// Dataview's `typeof()` names: "null", "boolean", "number", "string", "date", "duration",
    /// "link", "array", "object", "function".
    QString typeName() const;

private:
    std::variant<std::monostate, bool, double, QString, DateValue, Duration, LinkValue,
                 std::shared_ptr<const ValueList>, std::shared_ptr<const Object>, std::shared_ptr<const Function>>
            m;
};

/// A map of fields (Value::map(), frontmatter objects, `object()`).
class MapObject : public Object {
public:
    explicit MapObject(QList<std::pair<QString, Value>> fields);
    std::optional<Value> field(const QString &key) const override;
    QStringList keys() const override;
    const QList<std::pair<QString, Value>> &fields() const { return m_fields; }

private:
    QList<std::pair<QString, Value>> m_fields;
};

/// Dataview's ordering: values of different types by type (null first), numbers, durations,
/// dates by value, strings by text::compareNames() (then by code units), links by path, lists
/// element by element. <0, 0, >0.
int compare(const Value &a, const Value &b);
/// Equality as Dataview's `=`: the same type and value (strings exactly, links by path and
/// subpath, dates by instant, lists element-wise). Objects compare by their fields.
bool equals(const Value &a, const Value &b);

/// A canonical field key: lower-case, trimmed, blanks as '-', Markdown emphasis (`*`, `_`, `~`,
/// `` ` ``) removed ("Due Date" -> "due-date", "**Status**" -> "status").
QString canonicalKey(QStringView key);

/// A duration from text: "3 days", "1h 30m", "2 weeks", "1 year", "45 minut" (English units
/// and their short forms, Polish units); nullopt for anything else.
std::optional<Duration> parseDuration(QStringView text);

/// A date from text: ISO "2026-10-15", "2026-10-15T09:30[:ss]", "2026-10-15 09:30", "2026-10"
/// (the 1st); nullopt otherwise.
std::optional<DateValue> parseDate(QStringView text);

/// What a `key:: value` value or a frontmatter string means to Dataview: a link `[[…]]`, a date,
/// a number, a boolean, a duration, a list when every comma-separated part is a link (or a tag
/// for `tags`), else the trimmed text. `resolve` turns a link target into a LinkValue.
using LinkResolver = std::function<LinkValue(const QString &target, const QString &display, const QString &subpath,
                                             bool embed)>;
Value parseFieldValue(const QString &key, QStringView raw, const LinkResolver &resolve);
/// A `[[target#sub|display]]` (or `![[…]]`) link written alone; nullopt otherwise.
std::optional<Value> parseLinkText(QStringView text, const LinkResolver &resolve);

} // namespace brulion::dataview
