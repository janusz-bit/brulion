// The Dataview Query Language read into a Query (ast_p.h): a lexer and a recursive-descent parser.
#include "dataview/ast_p.h"
#include "dataview/eval_p.h"
#include "dataview/messages_p.h"

#include "util/paths.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

struct Token {
    enum class Kind { End, Ident, Number, String, Link, Tag, Op, Literal };
    Kind kind = Kind::End;
    QString text = {}; ///< identifier, string contents, link inside, tag, operator
    double number = 0;
    Value value = {};   ///< Literal: a date or duration written in date(…) / dur(…); DateWord in `text`
    bool embed = false; ///< Link: written ![[…]]
    int line = 1;
    qsizetype from = 0, to = 0;
};

struct ParseError {
    QString message;
    int line = 0;
};

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == u'_' || c.isSurrogate() || c.category() == QChar::Symbol_Other;
}

bool isIdentChar(QChar c)
{
    return isIdentStart(c) || c.isDigit() || c == u'-' || c.isMark();
}

bool isTagChar(QChar c)
{
    return !c.isSpace() && c != u'(' && c != u')' && c != u',' && c != u'"' && c != u'[' && c != u']' && c != u'!';
}

class Lexer {
public:
    explicit Lexer(const QString &src) : s(src) { }

    QList<Token> run()
    {
        QList<Token> out;
        while (true) {
            skipBlank();
            Token t{.line = line, .from = at};
            if (at >= s.size()) {
                t.to = at;
                out.append(t);
                return out;
            }
            const QChar c = s.at(at);
            if (c.isDigit()) {
                lexNumber(t);
            } else if (c == u'"') {
                lexString(t);
            } else if (c == u'#' && at + 1 < s.size() && isTagChar(s.at(at + 1))) {
                ++at;
                const qsizetype start = at;
                while (at < s.size() && isTagChar(s.at(at)))
                    ++at;
                t.kind = Token::Kind::Tag;
                t.text = s.sliced(start, at - start);
            } else if (ahead(u"[[") || ahead(u"![[")) {
                lexLink(t);
            } else if (isIdentStart(c)) {
                const qsizetype start = at;
                while (at < s.size() && isIdentChar(s.at(at)))
                    ++at;
                // A trailing '-' belongs to the next token ("a- b" is rare; "x-" never a name).
                while (at > start + 1 && s.at(at - 1) == u'-')
                    --at;
                t.kind = Token::Kind::Ident;
                t.text = s.sliced(start, at - start);
            } else {
                lexOperator(t);
            }
            t.to = at;
            out.append(t);
            if (t.kind == Token::Kind::Ident)
                lexRawArgument(t, out);
        }
    }

private:
    /// The text at the current position starts with `text` (a view: no copy of the rest).
    bool ahead(QStringView text) const { return QStringView(s).sliced(at).startsWith(text); }

    void skipBlank()
    {
        while (at < s.size()) {
            const QChar c = s.at(at);
            if (c == u'\n') {
                ++line;
                ++at;
            } else if (c.isSpace()) {
                ++at;
            } else if (c == u'/' && at + 1 < s.size() && s.at(at + 1) == u'/') {
                while (at < s.size() && s.at(at) != u'\n')
                    ++at;
            } else {
                return;
            }
        }
    }

    void lexNumber(Token &t)
    {
        const qsizetype start = at;
        while (at < s.size() && s.at(at).isDigit())
            ++at;
        if (at + 1 < s.size() && s.at(at) == u'.' && s.at(at + 1).isDigit()) {
            ++at;
            while (at < s.size() && s.at(at).isDigit())
                ++at;
        }
        t.kind = Token::Kind::Number;
        t.text = s.sliced(start, at - start);
        t.number = t.text.toDouble();
    }

    void lexString(Token &t)
    {
        ++at;
        QString out;
        while (true) {
            if (at >= s.size() || s.at(at) == u'\n')
                throw ParseError{messages::unclosedString(), t.line};
            const QChar c = s.at(at++);
            if (c == u'"')
                break;
            if (c == u'\\' && at < s.size()) {
                const QChar e = s.at(at++);
                out.append(e == u'n' ? u'\n' : e == u't' ? u'\t' : e);
                continue;
            }
            out.append(c);
        }
        t.kind = Token::Kind::String;
        t.text = out;
    }

    void lexLink(Token &t)
    {
        if (s.at(at) == u'!') {
            t.embed = true;
            ++at;
        }
        at += 2;
        const qsizetype end = s.indexOf(u"]]", at);
        // (A newline inside the link only: searching the rest of the text for one per link is quadratic.)
        if (end < 0 || QStringView(s).sliced(at, end - at).contains(u'\n'))
            throw ParseError{messages::unclosedLink(), t.line};
        t.kind = Token::Kind::Link;
        t.text = s.sliced(at, end - at);
        at = end + 2;
    }

    void lexOperator(Token &t)
    {
        static constexpr QStringView two[] = {u"!=", u"<=", u">=", u"=>", u"==", u"&&", u"||"};
        qsizetype length = 1;
        if (std::ranges::any_of(two, [this](QStringView op) { return ahead(op); }))
            length = 2;
        else if (!QStringView(u"()[]{},.+-*/%=<>!&|").contains(s.at(at)))
            throw ParseError{messages::unexpected(QString(s.at(at))), t.line};
        t.kind = Token::Kind::Op;
        t.text = s.sliced(at, length);
        at += length;
    }

    /// date(today), date(2026-10-15), date(2026-10-15T09:30), dur(3 days): the argument is a
    /// literal, not an expression. Appends "(", the literal and ")" then.
    void lexRawArgument(const Token &name, QList<Token> &out)
    {
        const bool isDate = name.text.compare(u"date", Qt::CaseInsensitive) == 0;
        if (!isDate && name.text.compare(u"dur", Qt::CaseInsensitive) != 0)
            return;
        qsizetype paren = at;
        while (paren < s.size() && s.at(paren).isSpace() && s.at(paren) != u'\n')
            ++paren;
        if (paren >= s.size() || s.at(paren) != u'(')
            return;
        // Up to the first ')' of the line, with no '(', '"' or ',' before it (else a call); a scan
        // that stops there keeps `date(date(date(…` linear.
        qsizetype close = paren + 1;
        while (close < s.size() && !QStringView(u")(\n\",").contains(s.at(close)))
            ++close;
        if (close >= s.size() || s.at(close) != u')')
            return;
        const QString raw = s.sliced(paren + 1, close - paren - 1).trimmed();
        if (raw.isEmpty())
            return;
        Token lit{.kind = Token::Kind::Literal, .line = line, .from = paren + 1, .to = close};
        if (isDate) {
            if (const QString word = raw.toLower(); isDateWord(word)) {
                lit.text = word;
            } else if (raw.front().isDigit()) {
                const auto date = parseDate(raw);
                if (!date)
                    throw ParseError{messages::badDate(raw), line};
                lit.value = Value::date(*date);
            } else {
                return;
            }
        } else {
            if (!raw.front().isDigit())
                return;
            const auto dur = parseDuration(raw);
            if (!dur)
                throw ParseError{messages::badDuration(raw), line};
            lit.value = Value::duration(*dur);
        }
        out.append(Token{.kind = Token::Kind::Op, .text = u"("_s, .line = line, .from = paren, .to = paren + 1});
        out.append(lit);
        out.append(Token{.kind = Token::Kind::Op, .text = u")"_s, .line = line, .from = close, .to = close + 1});
        at = close + 1;
    }

    const QString &s;
    qsizetype at = 0;
    int line = 1;
};

bool isKeyword(const Token &t, QStringView word)
{
    return t.kind == Token::Kind::Ident && t.text.compare(word, Qt::CaseInsensitive) == 0;
}

/// Words that start a data command: an expression stops before them.
bool isCommandStart(const Token &t)
{
    return isKeyword(t, u"from") || isKeyword(t, u"where") || isKeyword(t, u"sort") || isKeyword(t, u"group")
           || isKeyword(t, u"flatten") || isKeyword(t, u"limit");
}

class Parser {
public:
    Parser(const QString &src, QList<Token> tokens) : s(src), t(std::move(tokens)) { }

    Query run()
    {
        Query q;
        q.source = s;
        const Token &head = peek();
        if (head.kind == Token::Kind::End)
            throw ParseError{messages::emptyQuery(), 0};
        if (isKeyword(head, u"list"))
            q.type = QueryType::List;
        else if (isKeyword(head, u"table"))
            q.type = QueryType::Table;
        else if (isKeyword(head, u"task"))
            q.type = QueryType::Task;
        else if (isKeyword(head, u"calendar"))
            throw ParseError{messages::calendarNotSupported(), head.line};
        else
            throw ParseError{messages::expectedQueryType(describe(head)), head.line};
        next();
        if (q.type != QueryType::Task && isKeyword(peek(), u"without") && isKeyword(peek(1), u"id")) {
            next();
            next();
            q.withoutId = true;
        }
        if (q.type == QueryType::List && !atCommandOrEnd()) {
            const ExprPtr e = expression();
            q.columns.append({e, e->text});
        } else if (q.type == QueryType::Table && !atCommandOrEnd()) {
            while (true) {
                const ExprPtr e = expression();
                q.columns.append({e, alias(e->text)});
                if (!isOp(u","))
                    break;
                next();
            }
        }
        if (isKeyword(peek(), u"from")) {
            next();
            q.from = sourceOr();
        }
        while (peek().kind != Token::Kind::End) {
            const Token &c = peek();
            auto cmd = std::make_shared<Command>();
            cmd->line = c.line;
            if (isKeyword(c, u"where")) {
                next();
                cmd->kind = Command::Kind::Where;
                cmd->expr = expression();
            } else if (isKeyword(c, u"sort")) {
                next();
                cmd->kind = Command::Kind::Sort;
                while (true) {
                    SortKey key;
                    key.expr = expression();
                    if (isKeyword(peek(), u"asc") || isKeyword(peek(), u"ascending")) {
                        next();
                    } else if (isKeyword(peek(), u"desc") || isKeyword(peek(), u"descending")) {
                        next();
                        key.descending = true;
                    }
                    cmd->sort.append(key);
                    if (!isOp(u","))
                        break;
                    next();
                }
            } else if (isKeyword(c, u"group") && isKeyword(peek(1), u"by")) {
                next();
                next();
                cmd->kind = Command::Kind::GroupBy;
                cmd->expr = expression();
                cmd->name = alias(cmd->expr->text);
            } else if (isKeyword(c, u"flatten")) {
                next();
                cmd->kind = Command::Kind::Flatten;
                cmd->expr = expression();
                cmd->name = alias(cmd->expr->text);
            } else if (isKeyword(c, u"limit")) {
                next();
                cmd->kind = Command::Kind::Limit;
                const Token &n = peek();
                const bool whole = n.kind == Token::Kind::Number && !n.text.contains(u'.');
                if (!whole || n.number < 1 || n.number > MaxRows)
                    throw ParseError{messages::badLimit(describe(n), MaxRows), n.line};
                cmd->limit = int(n.number);
                next();
            } else if (isKeyword(c, u"from")) {
                throw ParseError{messages::fromNotFirst(), c.line};
            } else {
                throw ParseError{messages::unknownCommand(describe(c)), c.line};
            }
            q.commands.append(cmd);
        }
        return q;
    }

private:
    const Token &peek(int ahead = 0) const { return t.at(std::min(pos + ahead, t.size() - 1)); }
    const Token &next()
    {
        const Token &cur = t.at(pos);
        lastEnd = cur.to;
        if (pos < t.size() - 1)
            ++pos;
        return cur;
    }
    bool isOp(QStringView op, int ahead = 0) const
    {
        const Token &k = peek(ahead);
        return k.kind == Token::Kind::Op && k.text == op;
    }
    /// The next token starts a data command — `sort(…)` written right before a parenthesis is the
    /// function.
    bool atCommandOrEnd() const
    {
        if (peek().kind == Token::Kind::End)
            return true;
        return isCommandStart(peek()) && !(isOp(u"(", 1) && peek(1).from == peek().to);
    }

    QString describe(const Token &k) const
    {
        if (k.kind == Token::Kind::End)
            return QString();
        return s.sliced(k.from, k.to - k.from).simplified();
    }

    void expectOp(QStringView op)
    {
        if (!isOp(op))
            throw ParseError{messages::expectedClosing(op.toString(), describe(peek())), peek().line};
        next();
    }

    /// The name after AS (an identifier or a "string"), `fallback` without AS.
    QString alias(const QString &fallback)
    {
        if (!isKeyword(peek(), u"as"))
            return fallback;
        next();
        const Token &k = peek();
        if (k.kind != Token::Kind::Ident && k.kind != Token::Kind::String)
            throw ParseError{messages::expectedName(u"AS"_s), k.line};
        next();
        return k.text;
    }

    // ---- sources --------------------------------------------------------------------------------

    /// `a` (and `b`) combined; deeper than MaxExpressionDepth is an error at `line`.
    static std::shared_ptr<const Source> combined(Source::Kind kind, int line, std::shared_ptr<const Source> a,
                                                  std::shared_ptr<const Source> b = nullptr)
    {
        auto out = std::make_shared<Source>();
        out->kind = kind;
        out->depth = 1 + std::max(a->depth, b ? b->depth : 0);
        if (out->depth > MaxExpressionDepth)
            throw ParseError{messages::tooLong(MaxExpressionDepth), line};
        out->a = std::move(a);
        out->b = std::move(b);
        return out;
    }

    std::shared_ptr<const Source> sourceOr()
    {
        auto a = sourceAnd();
        while (isKeyword(peek(), u"or") || isOp(u"|") || isOp(u"||")) {
            const int line = next().line;
            a = combined(Source::Kind::Or, line, a, sourceAnd());
        }
        return a;
    }

    std::shared_ptr<const Source> sourceAnd()
    {
        auto a = sourceNot();
        while (isKeyword(peek(), u"and") || isOp(u"&") || isOp(u"&&")) {
            const int line = next().line;
            a = combined(Source::Kind::And, line, a, sourceNot());
        }
        return a;
    }

    std::shared_ptr<const Source> sourceNot()
    {
        Nesting n(*this); // negations and parentheses nest
        if (!isOp(u"-") && !isOp(u"!"))
            return sourceAtom();
        const int line = next().line;
        return combined(Source::Kind::Not, line, sourceNot());
    }

    std::shared_ptr<const Source> sourceAtom()
    {
        const Token &k = peek();
        auto out = std::make_shared<Source>();
        if (k.kind == Token::Kind::Tag) {
            next();
            out->kind = Source::Kind::Tag;
            out->value = k.text.normalized(QString::NormalizationForm_C).toLower();
            return out;
        }
        if (k.kind == Token::Kind::String) {
            next();
            out->kind = Source::Kind::Folder;
            QStringView path = QStringView(k.text).trimmed();
            while (path.startsWith(u'/'))
                path = path.sliced(1);
            while (path.endsWith(u'/'))
                path.chop(1);
            out->value = path.isEmpty() ? QString() : paths::normalize(path.toString());
            return out;
        }
        if (k.kind == Token::Kind::Link) {
            next();
            const QString target = linkTarget(k.text);
            out->kind = target.isEmpty() ? Source::Kind::Current : Source::Kind::Link;
            out->value = target;
            return out;
        }
        if (isKeyword(k, u"outgoing") && isOp(u"(", 1)) {
            next();
            next();
            const Token &l = peek();
            if (l.kind != Token::Kind::Link)
                throw ParseError{messages::badSource(describe(l)), l.line};
            next();
            expectOp(u")");
            out->kind = Source::Kind::Outgoing;
            out->value = linkTarget(l.text);
            return out;
        }
        if (isKeyword(k, u"csv"))
            throw ParseError{messages::csvNotSupported(), k.line};
        if (isOp(u"(")) {
            next();
            auto inner = sourceOr();
            expectOp(u")");
            return inner;
        }
        throw ParseError{messages::badSource(describe(k)), k.line};
    }

    /// The page of a [[link]] (no display, no heading); "" for [[]] / [[#]] (this page).
    static QString linkTarget(const QString &inner) { return inner.section(u'|', 0, 0).section(u'#', 0, 0).trimmed(); }

    // ---- expressions ----------------------------------------------------------------------------

    /// One level of nesting while it lives: past MaxExpressionNesting a ParseError (each level
    /// recurses through the whole precedence ladder).
    struct Nesting {
        explicit Nesting(Parser &p) : parser(p)
        {
            if (++parser.nesting > MaxExpressionNesting)
                throw ParseError{messages::tooDeep(), parser.peek().line};
        }
        ~Nesting() { --parser.nesting; }
        Parser &parser;
    };

    /// Gives `e` its parts: it is one level deeper than the deepest (an operator chain grows one
    /// level per operator). Past MaxExpressionDepth a ParseError at `line`.
    static void setArgs(Expr &e, QList<ExprPtr> args, int line)
    {
        for (const ExprPtr &a : std::as_const(args))
            e.depth = std::max(e.depth, a->depth + 1);
        if (e.depth > MaxExpressionDepth)
            throw ParseError{messages::tooLong(MaxExpressionDepth), line};
        e.args = std::move(args);
    }

    /// The query from `from` to the end of the last token read, blanks simplified.
    QString textFrom(qsizetype from) const
    {
        return s.sliced(from, std::max<qsizetype>(0, lastEnd - from)).simplified();
    }

    /// A node of `kind` with its text from `from` (none for from < 0: see binaryLoop()).
    std::shared_ptr<Expr> make(Expr::Kind kind, qsizetype from = -1)
    {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        if (from >= 0)
            e->text = textFrom(from);
        return e;
    }

    ExprPtr expression()
    {
        Nesting n(*this); // every nested expression comes through here: (…), [index], items, arguments, bodies
        return orExpr();
    }

    /// `operand (op operand)…`, left-associative. With a `word` (and, or) that word is an operator
    /// too and names them all, else an operator is named as written ("==" as "=").
    ExprPtr binaryLoop(ExprPtr (Parser::*operand)(), std::initializer_list<QStringView> ops, QStringView word = {})
    {
        const qsizetype from = peek().from;
        ExprPtr left = (this->*operand)();
        // Only the whole chain gets its text: nothing reads the texts of its inner nodes, and those
        // (`a + b`, `a + b + c`, …) would grow with the square of its length.
        std::shared_ptr<Expr> chain;
        while (true) {
            const auto op = std::ranges::find_if(ops, [this](QStringView o) { return isOp(o); });
            if (op == ops.end() && (word.isEmpty() || !isKeyword(peek(), word))) {
                if (chain)
                    chain->text = textFrom(from);
                return left;
            }
            const int line = next().line;
            chain = make(Expr::Kind::Binary);
            chain->name = word.isEmpty() ? op->toString() : word.toString();
            if (chain->name == u"==")
                chain->name = u"="_s;
            setArgs(*chain, {left, (this->*operand)()}, line);
            left = chain;
        }
    }

    ExprPtr orExpr() { return binaryLoop(&Parser::andExpr, {u"||", u"|"}, u"or"); }
    ExprPtr andExpr() { return binaryLoop(&Parser::compareExpr, {u"&&", u"&"}, u"and"); }
    ExprPtr compareExpr() { return binaryLoop(&Parser::addExpr, {u"!=", u"<=", u">=", u"==", u"=", u"<", u">"}); }
    ExprPtr addExpr() { return binaryLoop(&Parser::mulExpr, {u"+", u"-"}); }
    ExprPtr mulExpr() { return binaryLoop(&Parser::unaryExpr, {u"*", u"/", u"%"}); }

    ExprPtr unaryExpr()
    {
        const qsizetype from = peek().from;
        if (!isOp(u"-") && !isOp(u"!"))
            return postfixExpr();
        Nesting n(*this); // `- - x`: an operator nests the rest
        const Token &op = next();
        ExprPtr operand = unaryExpr();
        // -3 is a number.
        if (op.text == u"-" && operand->kind == Expr::Kind::Literal && operand->value.is(Value::Type::Number)) {
            auto e = make(Expr::Kind::Literal, from);
            e->value = Value::number(-operand->value.asNumber());
            return e;
        }
        auto e = make(Expr::Kind::Unary, from);
        e->name = op.text;
        setArgs(*e, {operand}, op.line);
        return e;
    }

    ExprPtr postfixExpr()
    {
        const qsizetype from = peek().from;
        ExprPtr e = primary();
        std::shared_ptr<Expr> chain; // only the whole chain gets its text, see binaryLoop()
        while (true) {
            if (isOp(u".")) {
                const int line = next().line;
                const Token &k = peek();
                if (k.kind != Token::Kind::Ident)
                    throw ParseError{messages::expectedName(u"."_s), k.line};
                next();
                chain = make(Expr::Kind::Field);
                chain->name = canonicalKey(k.text);
                setArgs(*chain, {e}, line);
            } else if (isOp(u"[")) {
                const int line = next().line;
                chain = make(Expr::Kind::Index);
                setArgs(*chain, {e, expression()}, line);
                expectOp(u"]");
            } else if (isOp(u"(")) {
                if (e->kind != Expr::Kind::Variable)
                    throw ParseError{messages::unexpected(u"("_s), peek().line};
                const int line = next().line;
                chain = make(Expr::Kind::Call);
                chain->name = e->name;
                setArgs(*chain, expressionList(u")"), line);
                checkCall(*chain, peek().line);
            } else {
                if (chain)
                    chain->text = textFrom(from);
                return e;
            }
            e = chain;
        }
    }

    /// Expressions separated by commas, then `closing`: a call's arguments, a list's items.
    QList<ExprPtr> expressionList(QStringView closing)
    {
        QList<ExprPtr> out;
        if (!isOp(closing)) {
            out.append(expression());
            while (isOp(u",")) {
                next();
                out.append(expression());
            }
        }
        expectOp(closing);
        return out;
    }

    static void checkCall(const Expr &call, int line)
    {
        const QString &name = call.name;
        if (isUnsupportedFunction(name))
            throw ParseError{messages::unsupportedFunction(name), line};
        const Builtin *b = findBuiltin(name);
        if (!b)
            throw ParseError{messages::unknownFunction(name), line};
        const int n = int(call.args.size());
        if (n < b->minArgs || (b->maxArgs >= 0 && n > b->maxArgs))
            throw ParseError{messages::wrongArgumentCount(name), line};
    }

    /// `(a, b) =>` ahead?
    bool lambdaAhead() const
    {
        if (!isOp(u"("))
            return false;
        int i = 1;
        if (!isOp(u")", i)) {
            while (true) {
                if (peek(i).kind != Token::Kind::Ident)
                    return false;
                ++i;
                if (isOp(u")", i))
                    break;
                if (!isOp(u",", i))
                    return false;
                ++i;
            }
        }
        return isOp(u"=>", i + 1);
    }

    ExprPtr primary()
    {
        const Token &k = peek();
        const qsizetype from = k.from;
        switch (k.kind) {
        case Token::Kind::Number: {
            next();
            auto e = make(Expr::Kind::Literal, from);
            e->value = Value::number(k.number);
            return e;
        }
        case Token::Kind::String: {
            next();
            auto e = make(Expr::Kind::Literal, from);
            e->value = Value::string(k.text);
            return e;
        }
        case Token::Kind::Literal: {
            next();
            auto e = make(k.text.isEmpty() ? Expr::Kind::Literal : Expr::Kind::DateWord, from);
            e->value = k.value;
            e->name = k.text;
            return e;
        }
        case Token::Kind::Link: {
            next();
            auto e = make(Expr::Kind::Link, from);
            QStringView inner = k.text;
            if (const qsizetype bar = inner.indexOf(u'|'); bar >= 0) {
                e->value = Value::string(inner.sliced(bar + 1).trimmed().toString());
                inner = inner.first(bar);
            }
            if (const qsizetype hash = inner.indexOf(u'#'); hash >= 0) {
                e->raw = inner.sliced(hash + 1).trimmed().toString();
                inner = inner.first(hash);
            }
            e->name = inner.trimmed().toString();
            e->params = k.embed ? QStringList{u"embed"_s} : QStringList();
            return e;
        }
        case Token::Kind::Ident: {
            if (isKeyword(k, u"true") || isKeyword(k, u"false")) {
                next();
                auto e = make(Expr::Kind::Literal, from);
                e->value = Value::boolean(isKeyword(k, u"true"));
                return e;
            }
            if (isKeyword(k, u"null")) {
                next();
                return make(Expr::Kind::Literal, from);
            }
            next();
            auto e = make(Expr::Kind::Variable, from);
            // A function name keeps its spelling for messages; built-ins are found lower-case.
            e->name = isOp(u"(") ? k.text.toLower() : canonicalKey(k.text);
            return e;
        }
        case Token::Kind::Op:
            if (lambdaAhead()) {
                next();
                QStringList params;
                while (!isOp(u")")) {
                    params.append(canonicalKey(next().text));
                    if (isOp(u","))
                        next();
                }
                next(); // )
                next(); // =>
                ExprPtr body = expression();
                auto e = make(Expr::Kind::Lambda, from);
                e->params = params;
                setArgs(*e, {body}, k.line);
                return e;
            }
            if (isOp(u"(")) {
                next();
                ExprPtr inner = expression();
                expectOp(u")");
                return inner;
            }
            if (isOp(u"[")) {
                next();
                QList<ExprPtr> items = expressionList(u"]");
                auto e = make(Expr::Kind::List, from);
                setArgs(*e, std::move(items), k.line);
                return e;
            }
            break;
        case Token::Kind::Tag:
        case Token::Kind::End: break;
        }
        throw ParseError{messages::expectedExpression(describe(k)), k.line};
    }

    const QString &s;
    QList<Token> t;
    qsizetype pos = 0;
    qsizetype lastEnd = 0;
    int nesting = 0;
};

} // namespace

std::variant<Query, QString> parseQuery(const QString &source)
{
    try {
        Lexer lexer(source);
        Parser parser(source, lexer.run());
        return parser.run();
    } catch (const ParseError &e) {
        if (e.line <= 0)
            return e.message;
        return messages::atLine(e.line, e.message);
    }
}

} // namespace brulion::dataview
