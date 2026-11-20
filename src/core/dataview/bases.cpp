// Obsidian Bases expressions (docs/SYNTAX.md §10 "Obsidian Bases"): their JavaScript-like syntax
// read by our own parser into the Dataview evaluator's tree — methods, Bases' own functions and
// its operators with their Bases meaning (`==` of links and files, `date + "1M"`) become calls of
// "b:" built-ins — and those built-ins. Nothing runs as a script; regular expressions are refused
// (a note synced from anywhere could make one run for minutes), so is HTML as markup.
#include "dataview/expression_p.h"
#include "dataview/messages_p.h"

#include "util/paths.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

using T = Value::Type;
using Args = ValueList;

QString tr(const char *text)
{
    return QCoreApplication::translate("brulion::Bases", text);
}

// ---- tokens ----------------------------------------------------------------------------------------

struct BToken {
    enum class Kind { End, Ident, Number, String, Op };
    Kind kind = Kind::End;
    QString text;
    double number = 0;
    qsizetype from = 0, to = 0;
};

struct BError {
    QString message;
};

bool identStart(QChar c)
{
    return c.isLetter() || c == u'_' || c == u'$' || c.isSurrogate();
}

bool identChar(QChar c)
{
    return identStart(c) || c.isDigit() || c.isMark();
}

QList<BToken> lex(const QString &s)
{
    QList<BToken> out;
    qsizetype i = 0;
    const qsizetype n = s.size();
    // An operand may come next: a '/' there starts a regular expression, not a division.
    const auto operandNext = [&out] {
        if (out.isEmpty())
            return true;
        const BToken &last = out.last();
        if (last.kind != BToken::Kind::Op)
            return false;
        return last.text != u")" && last.text != u"]";
    };
    while (true) {
        while (i < n && s[i].isSpace())
            ++i;
        BToken t;
        t.from = i;
        if (i >= n) {
            t.to = i;
            out.append(t);
            return out;
        }
        const QChar c = s[i];
        if (c.isDigit() || (c == u'.' && i + 1 < n && s[i + 1].isDigit())) {
            qsizetype j = i;
            while (j < n && s[j].isDigit())
                ++j;
            if (j < n && s[j] == u'.' && j + 1 < n && s[j + 1].isDigit()) {
                ++j;
                while (j < n && s[j].isDigit())
                    ++j;
            }
            if (j < n && (s[j] == u'e' || s[j] == u'E')) {
                qsizetype k = j + 1;
                if (k < n && (s[k] == u'+' || s[k] == u'-'))
                    ++k;
                if (k < n && s[k].isDigit()) {
                    while (k < n && s[k].isDigit())
                        ++k;
                    j = k;
                }
            }
            t.kind = BToken::Kind::Number;
            t.number = QStringView(s).sliced(i, j - i).toDouble();
            if (!std::isfinite(t.number))
                t.number = 0;
            i = j;
        } else if (c == u'"' || c == u'\'' || c == u'`') {
            QString text;
            qsizetype j = i + 1;
            bool closed = false;
            while (j < n) {
                const QChar d = s[j];
                if (d == c) {
                    closed = true;
                    ++j;
                    break;
                }
                if (d == u'\\' && j + 1 < n) {
                    const QChar e = s[j + 1];
                    text += e == u'n' ? u'\n' : e == u't' ? u'\t' : e;
                    j += 2;
                    continue;
                }
                text += d;
                ++j;
            }
            if (!closed)
                throw BError{messages::unclosedString()};
            t.kind = BToken::Kind::String;
            t.text = std::move(text);
            i = j;
        } else if (identStart(c)) {
            qsizetype j = i + 1;
            while (j < n && identChar(s[j]))
                ++j;
            t.kind = BToken::Kind::Ident;
            t.text = s.sliced(i, j - i);
            i = j;
        } else if (c == u'/' && operandNext()) {
            throw BError{tr("Regular expressions are not supported: a note from anywhere could make one run for "
                            "minutes. Use contains(), startsWith() or split() with a text.")};
        } else {
            static const QStringList twoChar{u"=="_s, u"!="_s, u"<="_s, u">="_s, u"&&"_s, u"||"_s, u"=>"_s};
            const QString two = s.sliced(i, std::min<qsizetype>(2, n - i));
            if (twoChar.contains(two)) {
                t.text = two;
                i += 2;
                if (t.text == u"==" || t.text == u"!=") // `===`, `!==`
                    if (i < n && s[i] == u'=')
                        ++i;
            } else if (QStringView(u"+-*/%<>!()[]{},.:?=").contains(c)) {
                t.text = QString(c);
                ++i;
            } else {
                throw BError{messages::unexpected(QString(c))};
            }
            t.kind = BToken::Kind::Op;
        }
        t.to = i;
        out.append(t);
    }
}

// ---- the functions a Bases expression may call ------------------------------------------------------

struct Signature {
    int minArgs = 0;
    int maxArgs = 0; ///< -1: any
};

/// Global functions (lower-case) and their arguments.
const QHash<QString, Signature> &globalFunctions()
{
    static const QHash<QString, Signature> table = {
            {u"if"_s, {2, 3}},       {u"date"_s, {1, 1}},  {u"duration"_s, {1, 1}}, {u"file"_s, {1, 1}},
            {u"link"_s, {1, 2}},     {u"list"_s, {1, 1}},  {u"max"_s, {1, -1}},     {u"min"_s, {1, -1}},
            {u"now"_s, {0, 0}},      {u"today"_s, {0, 0}}, {u"number"_s, {1, 1}},   {u"random"_s, {0, 0}},
            {u"escapehtml"_s, {1, 1}}, {u"html"_s, {1, 1}}, {u"image"_s, {1, 1}},   {u"icon"_s, {1, 1}},
    };
    return table;
}

/// Methods (lower-case) and their arguments (the receiver not counted).
const QHash<QString, Signature> &methods()
{
    static const QHash<QString, Signature> table = {
            {u"istruthy"_s, {0, 0}},   {u"istype"_s, {1, 1}},      {u"tostring"_s, {0, 0}},
            {u"isempty"_s, {0, 0}},    {u"contains"_s, {1, 1}},    {u"containsall"_s, {1, -1}},
            {u"containsany"_s, {1, -1}}, {u"startswith"_s, {1, 1}}, {u"endswith"_s, {1, 1}},
            {u"lower"_s, {0, 0}},      {u"upper"_s, {0, 0}},       {u"title"_s, {0, 0}},
            {u"trim"_s, {0, 0}},       {u"reverse"_s, {0, 0}},     {u"repeat"_s, {1, 1}},
            {u"slice"_s, {1, 2}},      {u"split"_s, {1, 2}},       {u"replace"_s, {2, 2}},
            {u"abs"_s, {0, 0}},        {u"ceil"_s, {0, 0}},        {u"floor"_s, {0, 0}},
            {u"round"_s, {0, 1}},      {u"tofixed"_s, {1, 1}},     {u"date"_s, {0, 0}},
            {u"format"_s, {1, 1}},     {u"time"_s, {0, 0}},        {u"relative"_s, {0, 0}},
            {u"filter"_s, {1, 1}},     {u"map"_s, {1, 1}},         {u"reduce"_s, {2, 2}},
            {u"flat"_s, {0, 0}},       {u"join"_s, {0, 1}},        {u"sort"_s, {0, 0}},
            {u"unique"_s, {0, 0}},     {u"asfile"_s, {0, 0}},      {u"linksto"_s, {1, 1}},
            {u"aslink"_s, {0, 1}},     {u"haslink"_s, {1, 1}},     {u"hasproperty"_s, {1, 1}},
            {u"hastag"_s, {1, -1}},    {u"infolder"_s, {1, 1}},    {u"keys"_s, {0, 0}},
            {u"values"_s, {0, 0}},     {u"mean"_s, {0, 0}},        {u"average"_s, {0, 0}},
            {u"sum"_s, {0, 0}},        {u"min"_s, {0, 0}},         {u"max"_s, {0, 0}},
            {u"median"_s, {0, 0}},     {u"stddev"_s, {0, 0}},      {u"length"_s, {0, 0}},
    };
    return table;
}

// ---- parser ------------------------------------------------------------------------------------------

class BParser {
public:
    BParser(const QString &src, QList<BToken> tokens) : s(src), t(std::move(tokens)) { }

    ParsedExpression run()
    {
        if (peek().kind == BToken::Kind::End)
            throw BError{messages::expectedExpression(QString())};
        ParsedExpression out;
        out.expr = expression();
        if (peek().kind != BToken::Kind::End)
            throw BError{messages::unexpected(describe(peek()))};
        out.formulas = m_formulas;
        out.property = propertyOf(*out.expr);
        return out;
    }

private:
    struct Nesting {
        explicit Nesting(BParser &p) : parser(p)
        {
            if (++parser.nesting > MaxExpressionNesting)
                throw BError{messages::tooDeep()};
        }
        ~Nesting() { --parser.nesting; }
        BParser &parser;
    };

    const BToken &peek(int ahead = 0) const { return t.at(std::min(pos + ahead, t.size() - 1)); }
    const BToken &next()
    {
        const BToken &cur = t.at(pos);
        if (pos < t.size() - 1)
            ++pos;
        return cur;
    }
    bool isOp(QStringView op, int ahead = 0) const
    {
        const BToken &k = peek(ahead);
        return k.kind == BToken::Kind::Op && k.text == op;
    }
    void expectOp(QStringView op)
    {
        if (!isOp(op))
            throw BError{messages::expectedClosing(op.toString(), describe(peek()))};
        next();
    }
    QString describe(const BToken &k) const
    {
        return k.kind == BToken::Kind::End ? QString() : s.sliced(k.from, k.to - k.from);
    }

    static std::shared_ptr<Expr> node(Expr::Kind kind)
    {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        return e;
    }
    static void setArgs(Expr &e, QList<ExprPtr> args)
    {
        for (const ExprPtr &a : std::as_const(args))
            e.depth = std::max(e.depth, a->depth + 1);
        if (e.depth > MaxExpressionDepth)
            throw BError{messages::tooLong(MaxExpressionDepth)};
        e.args = std::move(args);
    }
    static ExprPtr literal(const Value &v)
    {
        auto e = node(Expr::Kind::Literal);
        e->value = v;
        return e;
    }
    static ExprPtr variable(const QString &name)
    {
        auto e = node(Expr::Kind::Variable);
        e->name = name;
        return e;
    }
    static ExprPtr call(const QString &name, QList<ExprPtr> args)
    {
        auto e = node(Expr::Kind::Call);
        e->name = name;
        setArgs(*e, std::move(args));
        return e;
    }
    static ExprPtr lambda(const QStringList &params, ExprPtr body)
    {
        auto e = node(Expr::Kind::Lambda);
        e->params = params;
        setArgs(*e, {std::move(body)});
        return e;
    }
    static ExprPtr binary(const QString &op, ExprPtr a, ExprPtr b)
    {
        auto e = node(Expr::Kind::Binary);
        e->name = op;
        setArgs(*e, {std::move(a), std::move(b)});
        return e;
    }

    /// The property a lone name is (`koszt`, `note.status`, `note["Due date"]`), else "".
    static QString propertyOf(const Expr &e)
    {
        if (e.kind == Expr::Kind::Variable && !e.raw.isEmpty())
            return e.raw;
        if (e.kind == Expr::Kind::Call && e.name == u"b:field" && e.args.size() == 2
            && e.args.at(0)->kind == Expr::Kind::Variable && e.args.at(0)->name == u"row"
            && e.args.at(1)->kind == Expr::Kind::Literal)
            return e.args.at(1)->raw;
        return {};
    }

    ExprPtr expression()
    {
        Nesting n(*this);
        return orExpr();
    }
    ExprPtr orExpr()
    {
        ExprPtr a = andExpr();
        while (isOp(u"||")) {
            next();
            a = binary(u"or"_s, a, andExpr());
        }
        return a;
    }
    ExprPtr andExpr()
    {
        ExprPtr a = equalityExpr();
        while (isOp(u"&&")) {
            next();
            a = binary(u"and"_s, a, equalityExpr());
        }
        return a;
    }
    ExprPtr equalityExpr()
    {
        ExprPtr a = relationExpr();
        while (isOp(u"==") || isOp(u"!=") || isOp(u"=")) {
            const QString op = next().text;
            a = call(op == u"!=" ? u"b:ne"_s : u"b:eq"_s, {a, relationExpr()});
        }
        return a;
    }
    ExprPtr relationExpr()
    {
        ExprPtr a = additive();
        while (isOp(u"<") || isOp(u"<=") || isOp(u">") || isOp(u">=")) {
            const QString op = next().text;
            a = call(u"b:arith"_s, {literal(Value::string(op)), a, additive()});
        }
        return a;
    }
    ExprPtr additive()
    {
        ExprPtr a = multiplicative();
        while (isOp(u"+") || isOp(u"-")) {
            const QString op = next().text;
            a = call(u"b:arith"_s, {literal(Value::string(op)), a, multiplicative()});
        }
        return a;
    }
    ExprPtr multiplicative()
    {
        ExprPtr a = unary();
        while (isOp(u"*") || isOp(u"/") || isOp(u"%")) {
            const QString op = next().text;
            a = call(u"b:arith"_s, {literal(Value::string(op)), a, unary()});
        }
        return a;
    }
    ExprPtr unary()
    {
        if (isOp(u"!") || isOp(u"-") || isOp(u"+")) {
            Nesting n(*this);
            const QString op = next().text;
            ExprPtr operand = unary();
            if (op == u"+")
                return operand;
            if (op == u"-" && operand->kind == Expr::Kind::Literal && operand->value.is(T::Number))
                return literal(Value::number(-operand->value.asNumber()));
            auto e = node(Expr::Kind::Unary);
            e->name = op;
            setArgs(*e, {operand});
            return e;
        }
        return postfix();
    }

    /// Arguments up to `)`: a filter()/map()/reduce() expression argument becomes a lambda of
    /// `value`, `index` (and `acc`).
    QList<ExprPtr> arguments(const QString &method = QString())
    {
        QList<ExprPtr> out;
        if (!isOp(u")")) {
            while (true) {
                ExprPtr arg = expression();
                if (out.isEmpty() && (method == u"filter" || method == u"map"))
                    arg = lambda({u"value"_s, u"index"_s}, arg);
                else if (out.isEmpty() && method == u"reduce")
                    arg = lambda({u"acc"_s, u"value"_s, u"index"_s}, arg);
                out.append(arg);
                if (!isOp(u","))
                    break;
                next();
            }
        }
        expectOp(u")");
        return out;
    }

    static void checkCount(const QString &name, const Signature &sig, qsizetype n)
    {
        if (n < sig.minArgs || (sig.maxArgs >= 0 && n > sig.maxArgs))
            throw BError{messages::wrongArgumentCount(name)};
    }

    ExprPtr postfix()
    {
        ExprPtr e = primary();
        while (true) {
            if (isOp(u".")) {
                next();
                const BToken &k = peek();
                if (k.kind != BToken::Kind::Ident)
                    throw BError{messages::expectedName(u"."_s)};
                next();
                const QString name = k.text;
                if (isOp(u"(")) {
                    Nesting n(*this);
                    next();
                    const QString lower = name.toLower();
                    const auto it = methods().constFind(lower);
                    if (it == methods().cend())
                        throw BError{messages::unknownFunction(name)};
                    QList<ExprPtr> args = arguments(lower);
                    checkCount(name, *it, args.size());
                    args.prepend(e);
                    e = call(u"b:m:"_s + lower, std::move(args));
                    continue;
                }
                // A formula reads another formula (`formula.ppu`): noted for the order they run in.
                if (e->kind == Expr::Kind::Variable && e->name == u"formula") {
                    const QString key = canonicalKey(name);
                    if (!m_formulas.contains(key))
                        m_formulas.append(key);
                    auto f = node(Expr::Kind::Field);
                    f->name = key;
                    setArgs(*f, {e});
                    e = f;
                    continue;
                }
                auto lit = node(Expr::Kind::Literal);
                lit->value = Value::string(name);
                lit->raw = name;
                e = call(u"b:field"_s, {e, lit});
            } else if (isOp(u"[")) {
                Nesting n(*this);
                next();
                ExprPtr index = expression();
                expectOp(u"]");
                if (index->kind == Expr::Kind::Literal && index->value.is(T::String)) {
                    const QString name = index->value.asString();
                    if (e->kind == Expr::Kind::Variable && e->name == u"formula") {
                        const QString key = canonicalKey(name);
                        if (!m_formulas.contains(key))
                            m_formulas.append(key);
                    }
                    auto lit = node(Expr::Kind::Literal);
                    lit->value = Value::string(name);
                    lit->raw = name;
                    e = call(u"b:field"_s, {e, lit});
                } else {
                    auto ix = node(Expr::Kind::Index);
                    setArgs(*ix, {e, index});
                    e = ix;
                }
            } else if (isOp(u"(")) {
                throw BError{messages::unexpected(u"("_s)};
            } else {
                return e;
            }
        }
    }

    ExprPtr primary()
    {
        const BToken &k = peek();
        switch (k.kind) {
        case BToken::Kind::Number: next(); return literal(Value::number(k.number));
        case BToken::Kind::String: next(); return literal(Value::string(k.text));
        case BToken::Kind::Ident: {
            next();
            const QString name = k.text;
            if (isOp(u"(")) {
                Nesting n(*this);
                next();
                const QString lower = name.toLower();
                const auto it = globalFunctions().constFind(lower);
                if (it == globalFunctions().cend())
                    throw BError{messages::unknownFunction(name)};
                QList<ExprPtr> args = arguments();
                checkCount(name, *it, args.size());
                if (lower == u"html")
                    return call(u"b:html"_s, std::move(args));
                return call(u"b:"_s + lower, std::move(args));
            }
            if (name == u"true" || name == u"false")
                return literal(Value::boolean(name == u"true"));
            if (name == u"null" || name == u"undefined")
                return literal(Value());
            if (name == u"note")
                return variable(u"row"_s);
            if (name == u"file" || name == u"this" || name == u"formula" || name == u"values" || name == u"value"
                || name == u"index" || name == u"acc")
                return variable(name);
            auto v = node(Expr::Kind::Variable);
            v->name = canonicalKey(name);
            v->raw = name;
            return v;
        }
        case BToken::Kind::Op:
            if (k.text == u"(") {
                next();
                ExprPtr e = expression();
                expectOp(u")");
                return e;
            }
            if (k.text == u"[") {
                Nesting n(*this);
                next();
                QList<ExprPtr> items;
                if (!isOp(u"]")) {
                    while (true) {
                        items.append(expression());
                        if (!isOp(u","))
                            break;
                        next();
                    }
                }
                expectOp(u"]");
                auto e = node(Expr::Kind::List);
                setArgs(*e, std::move(items));
                return e;
            }
            if (k.text == u"{") {
                Nesting n(*this);
                next();
                QList<ExprPtr> parts;
                if (!isOp(u"}")) {
                    while (true) {
                        const BToken &key = peek();
                        if (key.kind != BToken::Kind::String && key.kind != BToken::Kind::Ident)
                            throw BError{messages::expectedName(u"{"_s)};
                        next();
                        parts.append(literal(Value::string(key.text)));
                        expectOp(u":");
                        parts.append(expression());
                        if (!isOp(u","))
                            break;
                        next();
                    }
                }
                expectOp(u"}");
                return call(u"object"_s, std::move(parts));
            }
            break;
        case BToken::Kind::End: break;
        }
        throw BError{messages::expectedExpression(describe(k))};
    }

    const QString &s;
    QList<BToken> t;
    qsizetype pos = 0;
    int nesting = 0;
    QStringList m_formulas;
};

// ---- helpers of the built-ins ---------------------------------------------------------------------

/// A file object (FileObject, AttachmentObject): it has the file's fields.
bool isFileObject(const Value &v)
{
    if (!v.is(T::Object))
        return false;
    const QStringList keys = v.asObject().keys();
    return keys.contains(u"outlinks"_s) && keys.contains(u"frontmatter"_s) && keys.contains(u"ext"_s);
}

/// The file object behind a value: itself, a page's or a row's `file`, a link's (or a path's)
/// note; null for anything else.
Value fileOf(const Value &v, Context &ctx)
{
    if (isFileObject(v))
        return v;
    if (v.is(T::Object)) {
        const std::optional<Value> f = v.asObject().field(u"file"_s);
        return f && isFileObject(*f) ? *f : Value();
    }
    if (v.is(T::Link) || v.is(T::String)) {
        LinkValue l = v.is(T::Link) ? v.asLink()
                                    : ctx.resolveLink(v.asString(), QString(), QString(), false,
                                                      ctx.options().currentPath);
        if (!l.resolved && v.is(T::Link))
            l = ctx.resolveLink(l.path, QString(), QString(), false, ctx.options().currentPath);
        if (!l.resolved)
            return Value();
        const Value row = ctx.fileRow(l.path);
        return fileOf(row, ctx);
    }
    return Value();
}

/// The path a value names (a file, a page, a link, a path or a name), "" for none.
QString pathOf(const Value &v, Context &ctx)
{
    const Value f = fileOf(v, ctx);
    if (f.isNull())
        return {};
    const std::optional<Value> path = f.asObject().field(u"path"_s);
    return path && path->is(T::String) ? path->asString() : QString();
}

/// Files, pages and rows compared as the links to them.
Value asLinkish(const Value &v, Context &ctx)
{
    if (v.is(T::Object)) {
        if (const QString path = pathOf(v, ctx); !path.isEmpty())
            return Value::link(LinkValue{path, QString(), QString(), false, true});
    }
    return v;
}

bool basesEquals(const Value &x, const Value &y, Context &ctx)
{
    const Value a = asLinkish(x, ctx);
    const Value b = asLinkish(y, ctx);
    if (a.is(T::Link) || b.is(T::Link)) {
        const auto target = [&](const Value &v) -> std::pair<QString, bool> {
            if (v.is(T::Link)) {
                const LinkValue &l = v.asLink();
                if (l.resolved)
                    return {l.path, true};
                const LinkValue r = ctx.resolveLink(l.path, QString(), QString(), false, ctx.options().currentPath);
                return {r.path, r.resolved};
            }
            if (v.is(T::String)) {
                const LinkValue r = ctx.resolveLink(v.asString(), QString(), QString(), false, ctx.options().currentPath);
                return {r.resolved ? r.path : v.asString(), r.resolved};
            }
            return {QString(), false};
        };
        if (!(a.is(T::Link) || a.is(T::String)) || !(b.is(T::Link) || b.is(T::String)))
            return false;
        const auto [pa, ra] = target(a);
        const auto [pb, rb] = target(b);
        return !pa.isEmpty() && pa == pb && ra == rb;
    }
    return equals(a, b);
}

QString text(const Value &v, Context &ctx)
{
    return v.is(T::String) ? v.asString() : toText(v, ctx);
}

/// A duration written as Bases writes them: "1M", "2h", "1 day", "3 weeks", "1y 2M 4d"; units y,
/// M (month), w, d, h, m (minute), s, ms and their English words; nullopt for anything else.
std::optional<Duration> basesDuration(QStringView s)
{
    s = s.trimmed();
    int sign = 1;
    if (s.startsWith(u'-')) {
        sign = -1;
        s = s.sliced(1).trimmed();
    } else if (s.startsWith(u'+')) {
        s = s.sliced(1).trimmed();
    }
    if (s.isEmpty())
        return std::nullopt;
    Duration d;
    qsizetype i = 0;
    bool any = false;
    while (i < s.size()) {
        while (i < s.size() && (s[i].isSpace() || s[i] == u','))
            ++i;
        if (i >= s.size())
            break;
        qsizetype j = i;
        while (j < s.size() && (s[j].isDigit() || s[j] == u'.'))
            ++j;
        if (j == i)
            return std::nullopt;
        bool ok = false;
        const double n = s.sliced(i, j - i).toDouble(&ok);
        if (!ok || n > 1e7)
            return std::nullopt;
        while (j < s.size() && s[j].isSpace())
            ++j;
        qsizetype k = j;
        while (k < s.size() && s[k].isLetter())
            ++k;
        const QString unit = s.sliced(j, k - j).toString();
        const QString lower = unit.toLower();
        const auto ms = [&](double per) { d.ms += qint64(std::llround(n * per)); };
        if (unit == u"M" || lower == u"month" || lower == u"months" || lower == u"mo")
            d.months += qint64(std::llround(n));
        else if (unit == u"y" || unit == u"Y" || lower == u"year" || lower == u"years")
            d.months += qint64(std::llround(n * 12));
        else if (unit == u"w" || unit == u"W" || lower == u"week" || lower == u"weeks")
            ms(7 * 86400000.0);
        else if (unit == u"d" || unit == u"D" || lower == u"day" || lower == u"days")
            ms(86400000.0);
        else if (unit == u"h" || unit == u"H" || lower == u"hour" || lower == u"hours")
            ms(3600000.0);
        else if (unit == u"m" || lower == u"minute" || lower == u"minutes" || lower == u"min")
            ms(60000.0);
        else if (unit == u"s" || unit == u"S" || lower == u"second" || lower == u"seconds")
            ms(1000.0);
        else if (lower == u"ms" || lower == u"millisecond" || lower == u"milliseconds")
            ms(1.0);
        else
            return std::nullopt;
        any = true;
        i = k;
    }
    if (!any)
        return std::nullopt;
    d.months *= sign;
    d.ms *= sign;
    return d;
}

DateValue shift(const DateValue &d, Duration dur, int sign)
{
    const DateValue out{d.at.addMonths(int(sign * dur.months)).addMSecs(sign * dur.ms),
                        d.hasTime || dur.ms % 86400000 != 0};
    return out;
}

/// "YYYY-MM-DD", "YYYY-MM-DD HH:mm[:ss]", "…THH:mm[:ss]" as a date.
std::optional<DateValue> parseBasesDate(QStringView t)
{
    t = t.trimmed();
    if (t.size() < 10)
        return std::nullopt;
    const QDate day = QDate::fromString(t.first(10).toString(), Qt::ISODate);
    if (!day.isValid())
        return std::nullopt;
    if (t.size() == 10)
        return DateValue{QDateTime(day, QTime(0, 0)), false};
    if (t[10] != u' ' && t[10] != u'T')
        return std::nullopt;
    QString time = t.sliced(11).toString();
    if (const qsizetype dot = time.indexOf(u'.'); dot >= 0)
        time.truncate(dot);
    if (time.endsWith(u'Z'))
        time.chop(1);
    QTime tm = QTime::fromString(time, u"HH:mm:ss"_s);
    if (!tm.isValid())
        tm = QTime::fromString(time, u"HH:mm"_s);
    if (!tm.isValid())
        return std::nullopt;
    return DateValue{QDateTime(day, tm), true};
}

/// Moment.js format tokens (Obsidian's `format()`): YYYY YY M MM MMM MMMM D DD Do d dd ddd dddd
/// H HH h hh m mm s ss SSS A a Q W WW X x, [literal text].
QString formatMoment(const DateValue &d, QStringView fmt)
{
    const QLocale c = QLocale::c();
    const QDateTime &at = d.at;
    const QDate date = at.date();
    const QTime time = at.time();
    QString out;
    qsizetype i = 0;
    const auto run = [&](QChar ch) {
        qsizetype n = 0;
        while (i + n < fmt.size() && fmt[i + n] == ch)
            ++n;
        return n;
    };
    const auto pad = [](qint64 v, int width) { return QString::number(v).rightJustified(width, u'0'); };
    while (i < fmt.size()) {
        const QChar ch = fmt[i];
        if (ch == u'[') {
            const qsizetype close = fmt.indexOf(u']', i + 1);
            if (close < 0) {
                out += fmt.sliced(i);
                break;
            }
            out += fmt.sliced(i + 1, close - i - 1);
            i = close + 1;
            continue;
        }
        const qsizetype n = run(ch);
        switch (ch.unicode()) {
        case u'Y': out += n == 2 ? pad(date.year() % 100, 2) : QString::number(date.year()); break;
        case u'M':
            out += n >= 4   ? c.monthName(date.month(), QLocale::LongFormat)
                   : n == 3 ? c.monthName(date.month(), QLocale::ShortFormat)
                            : pad(date.month(), int(n));
            break;
        case u'D':
            if (n == 1 && i + 2 < fmt.size() + 1 && fmt.sliced(i).startsWith(u"Do")) {
                const int day = date.day();
                const int r = day % 100;
                const char *suffix = r >= 11 && r <= 13 ? "th" : day % 10 == 1 ? "st" : day % 10 == 2 ? "nd" : day % 10 == 3 ? "rd" : "th";
                out += QString::number(day) + QLatin1String(suffix);
                i += 2;
                continue;
            }
            out += n >= 3 ? pad(date.dayOfYear(), 3) : pad(date.day(), int(n));
            break;
        case u'd':
            out += n >= 4   ? c.dayName(date.dayOfWeek(), QLocale::LongFormat)
                   : n == 3 ? c.dayName(date.dayOfWeek(), QLocale::ShortFormat)
                   : n == 2 ? c.dayName(date.dayOfWeek(), QLocale::NarrowFormat)
                            : QString::number(date.dayOfWeek() % 7);
            break;
        case u'H': out += pad(time.hour(), int(std::min<qsizetype>(n, 2))); break;
        case u'h': out += pad(time.hour() % 12 == 0 ? 12 : time.hour() % 12, int(std::min<qsizetype>(n, 2))); break;
        case u'm': out += pad(time.minute(), int(std::min<qsizetype>(n, 2))); break;
        case u's': out += pad(time.second(), int(std::min<qsizetype>(n, 2))); break;
        case u'S': out += pad(time.msec(), 3).left(int(n)); break;
        case u'A': out += time.hour() < 12 ? u"AM"_s : u"PM"_s; break;
        case u'a': out += time.hour() < 12 ? u"am"_s : u"pm"_s; break;
        case u'Q': out += QString::number((date.month() - 1) / 3 + 1); break;
        case u'W': out += pad(date.weekNumber(), int(std::min<qsizetype>(n, 2))); break;
        case u'X': out += QString::number(at.toSecsSinceEpoch()); break;
        case u'x': out += QString::number(at.toMSecsSinceEpoch()); break;
        default: out += fmt.sliced(i, n); break;
        }
        i += n;
    }
    return out;
}

/// "3 days ago", "in 2 hours" (translated) — the difference to now.
QString relative(const DateValue &d, Context &ctx)
{
    const QDateTime now = ctx.options().now.isValid() ? ctx.options().now : QDateTime::currentDateTime();
    const qint64 secs = now.secsTo(d.at);
    const qint64 a = std::abs(secs);
    QString amount;
    if (a < 45)
        amount = tr("a few seconds");
    else if (a < 45 * 60)
        amount = messages::minutes(int(std::max<qint64>(1, (a + 30) / 60)));
    else if (a < 22 * 3600)
        amount = messages::hours(int(std::max<qint64>(1, (a + 1800) / 3600)));
    else if (a < 26 * 86400)
        amount = messages::days(int(std::max<qint64>(1, (a + 43200) / 86400)));
    else if (a < 320 * 86400)
        amount = messages::months(int(std::max<qint64>(1, (a + 15 * 86400) / (30 * 86400))));
    else
        amount = messages::years(int(std::max<qint64>(1, (a + 182 * 86400) / (365 * 86400))));
    return secs >= 0 ? tr("in %1").arg(amount) : tr("%1 ago").arg(amount);
}

const ValueList &listOrEmpty(const Value &v)
{
    static const ValueList none;
    return v.is(T::List) ? v.asList() : none;
}

/// The numbers of a list (other values skipped).
QList<double> numbersOf(const Value &v)
{
    QList<double> out;
    for (const Value &item : listOrEmpty(v)) {
        if (item.is(T::Number) && std::isfinite(item.asNumber()))
            out.append(item.asNumber());
    }
    return out;
}

/// JavaScript's slice indices: negative from the end, clamped.
std::pair<qsizetype, qsizetype> sliceRange(qsizetype size, const Args &a)
{
    const auto at = [&](int i, qsizetype fallback) -> qsizetype {
        if (i >= a.size() || !a.at(i).is(T::Number) || std::isnan(a.at(i).asNumber()))
            return fallback;
        double n = std::trunc(a.at(i).asNumber());
        if (n < 0)
            n += double(size);
        return qsizetype(std::clamp(n, 0.0, double(size)));
    };
    const qsizetype from = at(1, 0);
    const qsizetype to = at(2, size);
    return {from, std::max(from, to)};
}

// ---- built-ins --------------------------------------------------------------------------------------

Value bIf(const Args &a, Context &)
{
    return a.at(0).truthy() ? a.at(1) : a.value(2);
}

Value bDate(const Args &a, Context &ctx)
{
    const Value &v = a.at(0);
    if (v.is(T::Date))
        return v;
    if (v.is(T::String)) {
        if (const auto d = parseBasesDate(v.asString()))
            return Value::date(*d);
        ctx.step();
    }
    return Value();
}

Value bDuration(const Args &a, Context &)
{
    if (a.at(0).is(T::Duration))
        return a.at(0);
    if (a.at(0).is(T::String)) {
        if (const auto d = basesDuration(a.at(0).asString()))
            return Value::duration(*d);
    }
    return Value();
}

Value bFile(const Args &a, Context &ctx)
{
    return fileOf(a.at(0), ctx);
}

Value bLink(const Args &a, Context &ctx)
{
    const Value &target = a.at(0);
    const QString display = a.size() > 1 && !a.at(1).isNull() ? text(a.at(1), ctx) : QString();
    if (isFileObject(target) || target.is(T::Object)) {
        const QString path = pathOf(target, ctx);
        return path.isEmpty() ? Value() : Value::link(LinkValue{path, display, QString(), false, true});
    }
    if (target.is(T::Link)) {
        LinkValue l = target.asLink();
        if (!display.isEmpty())
            l.display = display;
        return Value::link(l);
    }
    QString t = text(target, ctx).trimmed();
    if (t.startsWith(u"http://") || t.startsWith(u"https://"))
        return Value::string(display.isEmpty() ? t : u"["_s + display + u"]("_s + t + u')');
    if (t.startsWith(u"[[") && t.endsWith(u"]]"))
        t = t.sliced(2, t.size() - 4);
    QString subpath;
    if (const qsizetype bar = t.indexOf(u'|'); bar >= 0)
        t.truncate(bar);
    if (const qsizetype hash = t.indexOf(u'#'); hash >= 0) {
        subpath = t.sliced(hash + 1);
        t.truncate(hash);
    }
    if (t.isEmpty())
        return Value();
    return Value::link(ctx.resolveLink(t, display, subpath, false, ctx.options().currentPath));
}

Value bList(const Args &a, Context &ctx)
{
    if (a.at(0).is(T::List))
        return a.at(0);
    if (a.at(0).isNull())
        return ctx.list({});
    return ctx.list({a.at(0)});
}

Value extremeOf(const Args &a, int sign)
{
    const ValueList &items = a.size() == 1 && a.first().is(T::List) ? a.first().asList() : a;
    Value best;
    for (const Value &v : items) {
        if (v.isNull())
            continue;
        if (best.isNull() || compare(v, best) * sign > 0)
            best = v;
    }
    return best;
}

Value bMax(const Args &a, Context &)
{
    return extremeOf(a, 1);
}

Value bMin(const Args &a, Context &)
{
    return extremeOf(a, -1);
}

Value bNow(const Args &, Context &ctx)
{
    const QDateTime now = ctx.options().now.isValid() ? ctx.options().now : QDateTime::currentDateTime();
    return Value::date(DateValue{now, true});
}

Value bToday(const Args &, Context &ctx)
{
    const QDateTime now = ctx.options().now.isValid() ? ctx.options().now : QDateTime::currentDateTime();
    return Value::date(now.date());
}

Value bNumber(const Args &a, Context &)
{
    const Value &v = a.at(0);
    switch (v.type()) {
    case T::Number: return v;
    case T::Boolean: return Value::number(v.asBool() ? 1 : 0);
    case T::Date: return Value::number(double(v.asDate().at.toMSecsSinceEpoch()));
    case T::Duration: return Value::number(double(v.asDuration().ms + v.asDuration().months * 30LL * 86400000LL));
    case T::String: {
        bool ok = false;
        const double n = v.asString().trimmed().toDouble(&ok);
        return ok && std::isfinite(n) ? Value::number(n) : Value();
    }
    default: return Value();
    }
}

Value bRandom(const Args &, Context &)
{
    return Value::number(QRandomGenerator::global()->generateDouble());
}

Value bEscapeHtml(const Args &a, Context &ctx)
{
    return ctx.text(text(a.at(0), ctx).toHtmlEscaped());
}

/// html(): the text as it is — shown escaped like every text (never as markup).
Value bHtml(const Args &a, Context &ctx)
{
    return ctx.text(text(a.at(0), ctx));
}

Value bImage(const Args &a, Context &ctx)
{
    const Value &v = a.at(0);
    if (v.is(T::Link)) {
        LinkValue l = v.asLink();
        l.embed = true;
        return Value::link(l);
    }
    const QString t = text(v, ctx).trimmed();
    if (t.startsWith(u"http://") || t.startsWith(u"https://"))
        return Value::string(u"![]("_s + t + u')');
    Value link = bLink({Value::string(t)}, ctx);
    if (link.is(T::Link)) {
        LinkValue l = link.asLink();
        l.embed = true;
        return Value::link(l);
    }
    return link;
}

Value bIcon(const Args &a, Context &ctx)
{
    return Value::string(text(a.at(0), ctx));
}

Value bEq(const Args &a, Context &ctx)
{
    return Value::boolean(basesEquals(a.at(0), a.at(1), ctx));
}

Value bNe(const Args &a, Context &ctx)
{
    return Value::boolean(!basesEquals(a.at(0), a.at(1), ctx));
}

/// `a op b` with Bases' meaning: a date ± a duration text ("1M", "2 days"), a duration scaled by a
/// number; else as Dataview.
Value bArith(const Args &a, Context &ctx)
{
    const QString op = a.at(0).asString();
    const Value &x = a.at(1);
    const Value &y = a.at(2);
    if ((op == u"+" || op == u"-") && x.is(T::Date) && (y.is(T::String) || y.is(T::Duration))) {
        const std::optional<Duration> d = y.is(T::Duration) ? std::optional(y.asDuration()) : basesDuration(y.asString());
        if (d)
            return Value::date(shift(x.asDate(), *d, op == u"+" ? 1 : -1));
        if (op == u"-")
            return Value();
    }
    if ((op == u"*" || op == u"/") && x.is(T::Duration) && y.is(T::Number)) {
        const double n = y.asNumber();
        if (!std::isfinite(n) || (op == u"/" && n == 0))
            return Value();
        const double f = op == u"*" ? n : 1 / n;
        const Duration d = x.asDuration();
        return Value::duration(Duration{qint64(std::llround(double(d.months) * f)), qint64(std::llround(double(d.ms) * f))});
    }
    if (op == u"*" && x.is(T::Number) && y.is(T::Duration))
        return bArith({Value::string(op), y, x}, ctx);
    // Comparisons of dates with date texts ("2025-01-01"), as Obsidian reads them.
    if ((op == u"<" || op == u"<=" || op == u">" || op == u">=") && (x.is(T::Date) != y.is(T::Date))) {
        const Value &other = x.is(T::Date) ? y : x;
        if (other.is(T::String)) {
            if (const auto d = parseBasesDate(other.asString())) {
                const Value date = Value::date(*d);
                return applyOperator(op, x.is(T::Date) ? x : date, y.is(T::Date) ? y : date, ctx);
            }
        }
        return Value::boolean(false);
    }
    if (op == u"<" || op == u"<=" || op == u">" || op == u">=") {
        // Different kinds of values (a number and a text, a missing value) are never in order.
        if (x.isNull() || y.isNull() || x.type() != y.type())
            return Value::boolean(false);
    }
    return applyOperator(op, x, y, ctx);
}

/// `x.name`: Bases' file fields (name with its extension, basename, links, backlinks, embeds,
/// properties), `length` of texts and lists, the parts of a date, else the field.
Value bField(const Args &a, Context &ctx)
{
    const Value &x = a.at(0);
    const QString name = a.at(1).asString();
    ctx.step();
    if (name == u"length") {
        if (x.is(T::String))
            return Value::number(double(x.asString().size()));
        if (x.is(T::List))
            return Value::number(double(x.asList().size()));
    }
    if (x.is(T::Date)) {
        const QDateTime &at = x.asDate().at;
        if (name == u"year")
            return Value::number(at.date().year());
        if (name == u"month")
            return Value::number(at.date().month());
        if (name == u"day")
            return Value::number(at.date().day());
        if (name == u"hour")
            return Value::number(at.time().hour());
        if (name == u"minute")
            return Value::number(at.time().minute());
        if (name == u"second")
            return Value::number(at.time().second());
        if (name == u"millisecond")
            return Value::number(at.time().msec());
        return Value();
    }
    if (isFileObject(x)) {
        const Object &o = x.asObject();
        const auto f = [&](const QString &key) { return o.field(key).value_or(Value()); };
        if (name == u"name") {
            const Value base = f(u"name"_s);
            const Value ext = f(u"ext"_s);
            if (base.is(T::String) && ext.is(T::String) && !ext.asString().isEmpty())
                return Value::string(base.asString() + u'.' + ext.asString());
            return base;
        }
        if (name == u"basename")
            return f(u"name"_s);
        if (name == u"links")
            return f(u"outlinks"_s);
        if (name == u"backlinks")
            return f(u"inlinks"_s);
        if (name == u"properties")
            return f(u"frontmatter"_s);
        if (name == u"file")
            return x;
        if (name == u"embeds") {
            ValueList out;
            for (const Value &l : listOrEmpty(f(u"outlinks"_s))) {
                if (l.is(T::Link) && l.asLink().embed)
                    out.append(l);
            }
            return ctx.list(out);
        }
        return f(canonicalKey(name));
    }
    return fieldOf(x, canonicalKey(name), ctx);
}

Value bIsTruthy(const Args &a, Context &)
{
    return Value::boolean(a.at(0).truthy());
}

Value bIsType(const Args &a, Context &ctx)
{
    QString want = text(a.at(1), ctx).trimmed().toLower();
    if (want == u"array")
        want = u"list"_s;
    if (want == u"text")
        want = u"string"_s;
    return Value::boolean(basesTypeName(a.at(0)) == want);
}

Value bToString(const Args &a, Context &ctx)
{
    return ctx.text(text(a.at(0), ctx));
}

Value bIsEmpty(const Args &a, Context &ctx)
{
    const Value &v = a.at(0);
    switch (v.type()) {
    case T::Null: return Value::boolean(true);
    case T::String: return Value::boolean(v.asString().isEmpty());
    case T::List: return Value::boolean(v.asList().isEmpty());
    case T::Object:
        if (isFileObject(v))
            return Value::boolean(false);
        return Value::boolean(v.asObject().keys().isEmpty());
    default: Q_UNUSED(ctx); return Value::boolean(false);
    }
}

bool containsOne(const Value &hay, const Value &needle, Context &ctx)
{
    ctx.step();
    if (hay.is(T::String)) {
        ctx.stepText(hay.asString().size());
        return hay.asString().contains(text(needle, ctx));
    }
    if (hay.is(T::List))
        return std::ranges::any_of(hay.asList(), [&](const Value &item) { return basesEquals(item, needle, ctx); });
    if (hay.is(T::Link) && needle.is(T::String))
        return hay.asLink().path.contains(needle.asString());
    return false;
}

Value bContains(const Args &a, Context &ctx)
{
    return Value::boolean(containsOne(a.at(0), a.at(1), ctx));
}

Value bContainsAll(const Args &a, Context &ctx)
{
    for (qsizetype i = 1; i < a.size(); ++i) {
        if (!containsOne(a.at(0), a.at(i), ctx))
            return Value::boolean(false);
    }
    return Value::boolean(true);
}

Value bContainsAny(const Args &a, Context &ctx)
{
    for (qsizetype i = 1; i < a.size(); ++i) {
        if (containsOne(a.at(0), a.at(i), ctx))
            return Value::boolean(true);
    }
    return Value::boolean(false);
}

Value bStartsWith(const Args &a, Context &ctx)
{
    return Value::boolean(a.at(0).is(T::String) && a.at(0).asString().startsWith(text(a.at(1), ctx)));
}

Value bEndsWith(const Args &a, Context &ctx)
{
    return Value::boolean(a.at(0).is(T::String) && a.at(0).asString().endsWith(text(a.at(1), ctx)));
}

Value bLower(const Args &a, Context &ctx)
{
    return a.at(0).is(T::String) ? ctx.text(a.at(0).asString().toLower()) : Value();
}

Value bUpper(const Args &a, Context &ctx)
{
    return a.at(0).is(T::String) ? ctx.text(a.at(0).asString().toUpper()) : Value();
}

Value bTitle(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    QString s = a.at(0).asString();
    bool start = true;
    for (QChar &c : s) {
        if (c.isLetter()) {
            if (start)
                c = c.toUpper();
            start = false;
        } else {
            start = c.isSpace() || c == u'-';
        }
    }
    return ctx.text(s);
}

Value bTrim(const Args &a, Context &ctx)
{
    return a.at(0).is(T::String) ? ctx.text(a.at(0).asString().trimmed()) : Value();
}

Value bReverse(const Args &a, Context &ctx)
{
    if (a.at(0).is(T::String)) {
        QString s = a.at(0).asString();
        std::ranges::reverse(s);
        return ctx.text(s);
    }
    if (a.at(0).is(T::List)) {
        ValueList items = a.at(0).asList();
        std::ranges::reverse(items);
        return ctx.list(items);
    }
    return Value();
}

Value bRepeat(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String) || !a.at(1).is(T::Number))
        return Value();
    const double n = std::trunc(a.at(1).asNumber());
    if (!(n >= 0))
        return Value();
    const qint64 size = qint64(a.at(0).asString().size());
    checkSize(1, qint64(std::min(n, 1e12)) * std::max<qint64>(1, size));
    return ctx.text(a.at(0).asString().repeated(qsizetype(n)));
}

Value bSlice(const Args &a, Context &ctx)
{
    if (a.at(0).is(T::String)) {
        const QString &s = a.at(0).asString();
        const auto [from, to] = sliceRange(s.size(), a);
        return ctx.text(s.sliced(from, to - from));
    }
    if (a.at(0).is(T::List)) {
        const ValueList &items = a.at(0).asList();
        const auto [from, to] = sliceRange(items.size(), a);
        return ctx.list(items.sliced(from, to - from));
    }
    return Value();
}

Value bSplit(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString sep = text(a.at(1), ctx);
    const QString &s = a.at(0).asString();
    ctx.stepText(s.size());
    QStringList parts = sep.isEmpty() ? QStringList() : s.split(sep);
    if (sep.isEmpty()) {
        for (const QChar c : s)
            parts.append(QString(c));
    }
    checkSize(parts.size());
    if (a.size() > 2 && a.at(2).is(T::Number) && a.at(2).asNumber() >= 0)
        parts = parts.mid(0, qsizetype(std::min(a.at(2).asNumber(), double(parts.size()))));
    ValueList out;
    out.reserve(parts.size());
    for (const QString &p : std::as_const(parts))
        out.append(Value::string(p));
    return ctx.list(out);
}

Value bReplace(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString pattern = text(a.at(1), ctx);
    const QString with = text(a.at(2), ctx);
    const QString &s = a.at(0).asString();
    if (pattern.isEmpty())
        return a.at(0);
    const qint64 count = s.count(pattern);
    checkSize(1, s.size() + count * std::max<qint64>(0, with.size() - pattern.size()));
    ctx.stepText(s.size());
    return ctx.text(QString(s).replace(pattern, with));
}

Value bAbs(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::abs(a.at(0).asNumber())) : Value();
}

Value bCeil(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::ceil(a.at(0).asNumber())) : Value();
}

Value bFloor(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::floor(a.at(0).asNumber())) : Value();
}

Value bRound(const Args &a, Context &)
{
    if (!a.at(0).is(T::Number))
        return Value();
    const int digits = a.size() > 1 && a.at(1).is(T::Number) ? int(std::clamp(a.at(1).asNumber(), 0.0, 15.0)) : 0;
    const double f = std::pow(10.0, digits);
    return Value::number(std::round(a.at(0).asNumber() * f) / f);
}

Value bToFixed(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::Number))
        return Value();
    const int digits = a.at(1).is(T::Number) ? int(std::clamp(a.at(1).asNumber(), 0.0, 20.0)) : 0;
    return ctx.text(QString::number(a.at(0).asNumber(), 'f', digits));
}

Value bDateOnly(const Args &a, Context &)
{
    if (!a.at(0).is(T::Date))
        return Value();
    return Value::date(a.at(0).asDate().at.date());
}

Value bFormat(const Args &a, Context &ctx)
{
    if (a.at(0).is(T::Number)) // number.format() is not Bases'; a date is
        return Value();
    if (!a.at(0).is(T::Date))
        return Value();
    const QString fmt = text(a.at(1), ctx);
    ctx.stepText(fmt.size());
    return ctx.text(formatMoment(a.at(0).asDate(), fmt));
}

Value bTime(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::Date))
        return Value();
    return ctx.text(a.at(0).asDate().at.time().toString(u"HH:mm:ss"_s));
}

Value bRelative(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::Date))
        return Value();
    return ctx.text(relative(a.at(0).asDate(), ctx));
}

Value bFilter(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    ValueList out;
    const ValueList &items = a.at(0).asList();
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (callFunction(a.at(1).asFunction(), {items.at(i), Value::number(double(i))}, ctx).truthy())
            out.append(items.at(i));
    }
    return ctx.list(out);
}

Value bMap(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    ValueList out;
    Kept kept;
    const ValueList &items = a.at(0).asList();
    for (qsizetype i = 0; i < items.size(); ++i) {
        out.append(callFunction(a.at(1).asFunction(), {items.at(i), Value::number(double(i))}, ctx));
        kept.add(out.constLast());
    }
    return ctx.list(out);
}

Value bReduce(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    Value acc = a.at(2);
    const ValueList &items = a.at(0).asList();
    for (qsizetype i = 0; i < items.size(); ++i)
        acc = callFunction(a.at(1).asFunction(), {acc, items.at(i), Value::number(double(i))}, ctx);
    return acc;
}

void flattenInto(const ValueList &items, ValueList &out, Context &ctx, int depth)
{
    for (const Value &v : items) {
        ctx.step();
        if (v.is(T::List) && depth < MaxValueDepth)
            flattenInto(v.asList(), out, ctx, depth + 1);
        else
            out.append(v);
        checkSize(out.size());
    }
}

Value bFlat(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return Value();
    ValueList out;
    flattenInto(a.at(0).asList(), out, ctx, 0);
    return ctx.list(out);
}

Value bJoin(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return a.at(0).is(T::String) ? a.at(0) : Value();
    const QString sep = a.size() > 1 ? text(a.at(1), ctx) : u","_s;
    QString out;
    qint64 chars = 0;
    const ValueList &items = a.at(0).asList();
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString part = text(items.at(i), ctx);
        chars += part.size() + sep.size();
        checkSize(1, chars);
        if (i > 0)
            out += sep;
        out += part;
    }
    return ctx.text(out);
}

Value bSort(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return Value();
    ValueList items = a.at(0).asList();
    ctx.step(items.size());
    std::ranges::stable_sort(items, [](const Value &x, const Value &y) { return compare(x, y) < 0; });
    return ctx.list(items);
}

Value bUnique(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return Value();
    ValueList out;
    for (const Value &v : a.at(0).asList()) {
        ctx.step(1 + out.size() / 64);
        if (std::ranges::none_of(out, [&](const Value &o) { return basesEquals(o, v, ctx); }))
            out.append(v);
    }
    return ctx.list(out);
}

Value bAsFile(const Args &a, Context &ctx)
{
    return fileOf(a.at(0), ctx);
}

bool linksTo(const Value &from, const Value &to, Context &ctx)
{
    const Value file = fileOf(from, ctx);
    const QString target = pathOf(to, ctx);
    if (file.isNull())
        return false;
    for (const Value &l : listOrEmpty(file.asObject().field(u"outlinks"_s).value_or(Value()))) {
        if (!l.is(T::Link))
            continue;
        if (!target.isEmpty() ? l.asLink().path == target : basesEquals(l, to, ctx))
            return true;
    }
    return false;
}

Value bLinksTo(const Args &a, Context &ctx)
{
    return Value::boolean(linksTo(a.at(0), a.at(1), ctx));
}

Value bAsLink(const Args &a, Context &ctx)
{
    const QString path = pathOf(a.at(0), ctx);
    if (path.isEmpty())
        return Value();
    const QString display = a.size() > 1 && !a.at(1).isNull() ? text(a.at(1), ctx) : QString();
    return Value::link(LinkValue{path, display, QString(), false, true});
}

Value bHasLink(const Args &a, Context &ctx)
{
    return Value::boolean(linksTo(a.at(0), a.at(1), ctx));
}

Value bHasProperty(const Args &a, Context &ctx)
{
    const Value file = fileOf(a.at(0), ctx);
    if (file.isNull())
        return Value::boolean(false);
    const Value props = file.asObject().field(u"frontmatter"_s).value_or(Value());
    if (!props.is(T::Object))
        return Value::boolean(false);
    const QString key = canonicalKey(text(a.at(1), ctx));
    return Value::boolean(props.asObject().field(key).has_value());
}

Value bHasTag(const Args &a, Context &ctx)
{
    const Value file = fileOf(a.at(0), ctx);
    if (file.isNull())
        return Value::boolean(false);
    QStringList tags;
    for (const Value &t : listOrEmpty(file.asObject().field(u"etags"_s).value_or(Value()))) {
        if (t.is(T::String))
            tags.append(t.asString().startsWith(u'#') ? t.asString().sliced(1).toLower() : t.asString().toLower());
    }
    for (qsizetype i = 1; i < a.size(); ++i) {
        QString want = text(a.at(i), ctx).trimmed().toLower();
        if (want.startsWith(u'#'))
            want.remove(0, 1);
        if (want.isEmpty())
            continue;
        const QString prefix = want + u'/';
        if (std::ranges::any_of(tags, [&](const QString &t) { return t == want || t.startsWith(prefix); }))
            return Value::boolean(true);
    }
    return Value::boolean(false);
}

Value bInFolder(const Args &a, Context &ctx)
{
    const QString path = pathOf(a.at(0), ctx);
    if (path.isEmpty())
        return Value::boolean(false);
    QString folder = text(a.at(1), ctx).trimmed();
    while (folder.endsWith(u'/'))
        folder.chop(1);
    while (folder.startsWith(u'/'))
        folder.remove(0, 1);
    if (folder.isEmpty())
        return Value::boolean(true);
    return Value::boolean(path.startsWith(folder + u'/', Qt::CaseInsensitive));
}

Value bKeys(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::Object))
        return Value();
    ValueList out;
    for (const QString &k : a.at(0).asObject().keys())
        out.append(Value::string(k));
    return ctx.list(out);
}

Value bValues(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::Object))
        return Value();
    ValueList out;
    const Object &o = a.at(0).asObject();
    for (const QString &k : o.keys())
        out.append(o.field(k).value_or(Value()));
    return ctx.list(out);
}

Value bMean(const Args &a, Context &)
{
    const QList<double> n = numbersOf(a.at(0));
    if (n.isEmpty())
        return Value();
    double sum = 0;
    for (const double v : n)
        sum += v;
    return Value::number(sum / double(n.size()));
}

Value bSum(const Args &a, Context &)
{
    double sum = 0;
    for (const double v : numbersOf(a.at(0)))
        sum += v;
    return Value::number(sum);
}

Value bListMin(const Args &a, Context &)
{
    return extremeOf({a.at(0)}, -1);
}

Value bListMax(const Args &a, Context &)
{
    return extremeOf({a.at(0)}, 1);
}

Value bMedian(const Args &a, Context &)
{
    QList<double> n = numbersOf(a.at(0));
    if (n.isEmpty())
        return Value();
    std::ranges::sort(n);
    const qsizetype mid = n.size() / 2;
    return Value::number(n.size() % 2 ? n.at(mid) : (n.at(mid - 1) + n.at(mid)) / 2);
}

Value bStddev(const Args &a, Context &)
{
    const QList<double> n = numbersOf(a.at(0));
    if (n.isEmpty())
        return Value();
    double mean = 0;
    for (const double v : n)
        mean += v;
    mean /= double(n.size());
    double sq = 0;
    for (const double v : n)
        sq += (v - mean) * (v - mean);
    return Value::number(std::sqrt(sq / double(n.size())));
}

Value bLength(const Args &a, Context &ctx)
{
    return bField({a.at(0), Value::string(u"length"_s)}, ctx);
}

const QHash<QString, Builtin> &basesBuiltins()
{
    static const QHash<QString, Builtin> table = {
            {u"b:if"_s, {2, 3, bIf}},
            {u"b:date"_s, {1, 1, bDate}},
            {u"b:duration"_s, {1, 1, bDuration}},
            {u"b:file"_s, {1, 1, bFile}},
            {u"b:link"_s, {1, 2, bLink}},
            {u"b:list"_s, {1, 1, bList}},
            {u"b:max"_s, {1, -1, bMax}},
            {u"b:min"_s, {1, -1, bMin}},
            {u"b:now"_s, {0, 0, bNow}},
            {u"b:today"_s, {0, 0, bToday}},
            {u"b:number"_s, {1, 1, bNumber}},
            {u"b:random"_s, {0, 0, bRandom}},
            {u"b:escapehtml"_s, {1, 1, bEscapeHtml}},
            {u"b:html"_s, {1, 1, bHtml}},
            {u"b:image"_s, {1, 1, bImage}},
            {u"b:icon"_s, {1, 1, bIcon}},
            {u"b:eq"_s, {2, 2, bEq}},
            {u"b:ne"_s, {2, 2, bNe}},
            {u"b:arith"_s, {3, 3, bArith}},
            {u"b:field"_s, {2, 2, bField}},
            {u"b:m:istruthy"_s, {1, 1, bIsTruthy}},
            {u"b:m:istype"_s, {2, 2, bIsType}},
            {u"b:m:tostring"_s, {1, 1, bToString}},
            {u"b:m:isempty"_s, {1, 1, bIsEmpty}},
            {u"b:m:contains"_s, {2, 2, bContains}},
            {u"b:m:containsall"_s, {2, -1, bContainsAll}},
            {u"b:m:containsany"_s, {2, -1, bContainsAny}},
            {u"b:m:startswith"_s, {2, 2, bStartsWith}},
            {u"b:m:endswith"_s, {2, 2, bEndsWith}},
            {u"b:m:lower"_s, {1, 1, bLower}},
            {u"b:m:upper"_s, {1, 1, bUpper}},
            {u"b:m:title"_s, {1, 1, bTitle}},
            {u"b:m:trim"_s, {1, 1, bTrim}},
            {u"b:m:reverse"_s, {1, 1, bReverse}},
            {u"b:m:repeat"_s, {2, 2, bRepeat}},
            {u"b:m:slice"_s, {2, 3, bSlice}},
            {u"b:m:split"_s, {2, 3, bSplit}},
            {u"b:m:replace"_s, {3, 3, bReplace}},
            {u"b:m:abs"_s, {1, 1, bAbs}},
            {u"b:m:ceil"_s, {1, 1, bCeil}},
            {u"b:m:floor"_s, {1, 1, bFloor}},
            {u"b:m:round"_s, {1, 2, bRound}},
            {u"b:m:tofixed"_s, {2, 2, bToFixed}},
            {u"b:m:date"_s, {1, 1, bDateOnly}},
            {u"b:m:format"_s, {2, 2, bFormat}},
            {u"b:m:time"_s, {1, 1, bTime}},
            {u"b:m:relative"_s, {1, 1, bRelative}},
            {u"b:m:filter"_s, {2, 2, bFilter}},
            {u"b:m:map"_s, {2, 2, bMap}},
            {u"b:m:reduce"_s, {3, 3, bReduce}},
            {u"b:m:flat"_s, {1, 1, bFlat}},
            {u"b:m:join"_s, {1, 2, bJoin}},
            {u"b:m:sort"_s, {1, 1, bSort}},
            {u"b:m:unique"_s, {1, 1, bUnique}},
            {u"b:m:asfile"_s, {1, 1, bAsFile}},
            {u"b:m:linksto"_s, {2, 2, bLinksTo}},
            {u"b:m:aslink"_s, {1, 2, bAsLink}},
            {u"b:m:haslink"_s, {2, 2, bHasLink}},
            {u"b:m:hasproperty"_s, {2, 2, bHasProperty}},
            {u"b:m:hastag"_s, {2, -1, bHasTag}},
            {u"b:m:infolder"_s, {2, 2, bInFolder}},
            {u"b:m:keys"_s, {1, 1, bKeys}},
            {u"b:m:values"_s, {1, 1, bValues}},
            {u"b:m:mean"_s, {1, 1, bMean}},
            {u"b:m:average"_s, {1, 1, bMean}},
            {u"b:m:sum"_s, {1, 1, bSum}},
            {u"b:m:min"_s, {1, 1, bListMin}},
            {u"b:m:max"_s, {1, 1, bListMax}},
            {u"b:m:median"_s, {1, 1, bMedian}},
            {u"b:m:stddev"_s, {1, 1, bStddev}},
            {u"b:m:length"_s, {1, 1, bLength}},
    };
    return table;
}

} // namespace

std::variant<ParsedExpression, QString> parseBasesExpression(const QString &source)
{
    try {
        BParser parser(source, lex(source));
        return parser.run();
    } catch (const BError &e) {
        return e.message;
    }
}

const Builtin *findBasesBuiltin(const QString &name)
{
    const auto &table = basesBuiltins();
    const auto it = table.constFind(name);
    return it == table.constEnd() ? nullptr : &*it;
}

QString basesTypeName(const Value &value)
{
    switch (value.type()) {
    case T::Null: return u"null"_s;
    case T::Boolean: return u"boolean"_s;
    case T::Number: return u"number"_s;
    case T::String: return u"string"_s;
    case T::Date: return u"date"_s;
    case T::Duration: return u"duration"_s;
    case T::Link: return u"link"_s;
    case T::List: return u"list"_s;
    case T::Function: return u"function"_s;
    case T::Object: return isFileObject(value) ? u"file"_s : u"object"_s;
    }
    return {};
}

} // namespace brulion::dataview
