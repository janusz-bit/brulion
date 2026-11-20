// A JSON reader that keeps where everything is (internal to the canvas module): every value with
// its span in the text, objects with their members in the order written (duplicates too), arrays
// with their items — what writing a canvas back byte for byte needs (QJsonObject sorts its keys
// and forgets the layout). Bounded: nesting depth and the number of values are limited, so a
// hostile file cannot make it allocate without end. Plus JSON.stringify's writing of strings and
// numbers.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>

namespace brulion::canvas::detail {

/// Deepest nesting read (a canvas needs 3: the root, its arrays, the items; unknown values of
/// plugins a few more).
inline constexpr int MaxJsonDepth = 64;
/// Most values read in one text (every number, string, array and object counts).
inline constexpr qsizetype MaxJsonValues = 400'000;

struct JsonValue;

/// A member of an object: `"key": value` with the whitespace before it.
struct JsonMember {
    QString key;           ///< decoded
    qsizetype leadFrom = 0; ///< the whitespace before the key starts here (after `{` or `,`)
    qsizetype keyFrom = 0;  ///< the key's opening quote
    qsizetype keyTo = 0;    ///< after its closing quote
    std::shared_ptr<JsonValue> value;
};

struct JsonValue {
    enum class Kind : quint8 { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    qsizetype from = 0; ///< the value's first character
    qsizetype to = 0;   ///< after its last one
    bool boolean = false;
    double number = 0;
    QString string; ///< decoded (strings)
    /// Arrays: the items; objects: the members' values are in `members`.
    QList<std::shared_ptr<JsonValue>> items;
    /// Where each item's leading whitespace starts (after `[` or `,`), arrays only.
    QList<qsizetype> itemLeads;
    QList<JsonMember> members;
    /// Where the whitespace before the closing bracket starts (arrays and objects).
    qsizetype closeFrom = 0;

    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    /// The last member named `key` (JSON.parse keeps the last of duplicates), or null.
    const JsonMember *member(QStringView key) const;
};

/// Why a text could not be read.
enum class JsonError { None, Syntax, TooDeep, TooLarge };

struct JsonDocument {
    std::shared_ptr<JsonValue> root;
    qsizetype rootFrom = 0; ///< where the root value starts (after whitespace and a BOM)
    JsonError error = JsonError::None;
};

/// Parse `text` (one JSON value, whitespace around it allowed; a leading BOM skipped).
JsonDocument parseJson(QStringView text);

/// `s` as JSON.stringify writes it (quotes included; non-ASCII written as it is, control
/// characters and lone surrogates escaped).
QString jsonString(QStringView s);
/// A number as JSON.stringify writes it: integers without a fraction, others in their shortest
/// form; non-finite numbers as 0 (JSON has none).
QString jsonNumber(double v);

} // namespace brulion::canvas::detail
