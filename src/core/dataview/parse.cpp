// The Dataview Query Language read into a Query (ast_p.h): a lexer and a recursive-descent parser.
#include "dataview/ast_p.h"
#include "dataview/eval_p.h"
#include "dataview/messages_p.h"

#include "util/paths.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

constexpr int MaxDepth = 64;

struct Token {
    enum class Kind { End, Ident, Number, String, Link, Tag, Op, Literal };
    Kind kind = Kind::End;
    QString text;  ///< identifier, string contents, link inside, tag, operator
    double number = 0;
    Value value;   ///< Literal: a date or duration written in date(…) / dur(…); DateWord in `text`
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
            Token t;
            t.line = line;
            t.from = at;
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
            } else if (s.sliced(at).startsWith(u"[[") || s.sliced(at).startsWith(u"![[")) {
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
                t.to = at;
                out.append(t);
                if (lexRawArgument(t, out))
                    continue;
                continue;
            } else {
                lexOperator(t);
            }
            t.to = at;
            out.append(t);
        }
    }

private:
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
        const qsizetype newline = s.indexOf(u'\n', at);
        if (end < 0 || (newline >= 0 && newline < end))
            throw ParseError{messages::unclosedLink(), t.line};
        t.kind = Token::Kind::Link;
        t.text = s.sliced(at, end - at);
        at = end + 2;
    }

    void lexOperator(Token &t)
    {
        static const char *const two[] = {"!=", "<=", ">=", "=>", "==", "&&", "||"};
        for (const char *op : two) {
            if (at + 1 < s.size() && s.at(at) == QLatin1Char(op[0]) && s.at(at + 1) == QLatin1Char(op[1])) {
                t.kind = Token::Kind::Op;
                t.text = QString::fromLatin1(op);
                at += 2;
                return;
            }
        }
        const QChar c = s.at(at);
        if (QStringView(u"()[]{},.+-*/%=<>!&|").contains(c)) {
            t.kind = Token::Kind::Op;
            t.text = QString(c);
            ++at;
            return;
        }
        throw ParseError{messages::unexpected(QString(c)), t.line};
    }

    /// date(today), date(2026-10-15), date(2026-10-15T09:30), dur(3 days): the argument is a
    /// literal, not an expression. Appends "(", the literal and ")" and returns true then.
    bool lexRawArgument(const Token &name, QList<Token> &out)
    {
        const QString fn = name.text.toLower();
        if (fn != u"date" && fn != u"dur")
            return false;
        qsizetype i = at;
        while (i < s.size() && s.at(i).isSpace() && s.at(i) != u'\n')
            ++i;
        if (i >= s.size() || s.at(i) != u'(')
            return false;
        const qsizetype close = s.indexOf(u')', i + 1);
        const qsizetype newline = s.indexOf(u'\n', i + 1);
        if (close < 0 || (newline >= 0 && newline < close))
            return false;
        const QString raw = s.sliced(i + 1, close - i - 1).trimmed();
        if (raw.isEmpty() || raw.contains(u'(') || raw.contains(u'"') || raw.contains(u','))
            return false;
        Token lit;
        lit.kind = Token::Kind::Literal;
        lit.line = line;
        lit.from = i + 1;
        lit.to = close;
        if (fn == u"date") {
            if (isDateWord(raw.toLower())) {
                lit.text = raw.toLower();
            } else if (raw.front().isDigit()) {
                const auto date = parseDate(raw);
                if (!date)
                    throw ParseError{messages::badDate(raw), line};
                lit.value = Value::date(*date);
            } else {
                return false;
            }
        } else {
            if (!raw.front().isDigit())
                return false;
            const auto dur = parseDuration(raw);
            if (!dur)
                throw ParseError{messages::badDuration(raw), line};
            lit.value = Value::duration(*dur);
        }
        Token open;
        open.kind = Token::Kind::Op;
        open.text = u"("_s;
        open.line = line;
        open.from = i;
        open.to = i + 1;
        Token closing = open;
        closing.text = u")"_s;
        closing.from = close;
        closing.to = close + 1;
        out.append(open);
        out.append(lit);
        out.append(closing);
        at = close + 1;
        return true;
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
                QString header = e->text;
                if (isKeyword(peek(), u"as")) {
                    next();
                    header = name(u"AS"_s);
                }
                q.columns.append({e, header});
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
                cmd->name = cmd->expr->text;
                if (isKeyword(peek(), u"as")) {
                    next();
                    cmd->name = name(u"AS"_s);
                }
            } else if (isKeyword(c, u"flatten")) {
                next();
                cmd->kind = Command::Kind::Flatten;
                cmd->expr = expression();
                cmd->name = cmd->expr->text;
                if (isKeyword(peek(), u"as")) {
                    next();
                    cmd->name = name(u"AS"_s);
                }
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
    const Token &peek(int ahead = 0) const
    {
        const qsizetype i = std::min(pos + ahead, t.size() - 1);
        return t.at(i);
    }
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

    /// The name after AS: an identifier or a "string".
    QString name(const QString &after)
    {
        const Token &k = peek();
        if (k.kind != Token::Kind::Ident && k.kind != Token::Kind::String)
            throw ParseError{messages::expectedName(after), k.line};
        next();
        return k.text;
    }

    // ---- sources --------------------------------------------------------------------------------

    std::shared_ptr<const Source> sourceOr()
    {
        auto a = sourceAnd();
        while (isKeyword(peek(), u"or") || isOp(u"|") || isOp(u"||")) {
            next();
            auto s2 = std::make_shared<Source>();
            s2->kind = Source::Kind::Or;
            s2->a = a;
            s2->b = sourceAnd();
            a = s2;
        }
        return a;
    }

    std::shared_ptr<const Source> sourceAnd()
    {
        auto a = sourceNot();
        while (isKeyword(peek(), u"and") || isOp(u"&") || isOp(u"&&")) {
            next();
            auto s2 = std::make_shared<Source>();
            s2->kind = Source::Kind::And;
            s2->a = a;
            s2->b = sourceNot();
            a = s2;
        }
        return a;
    }

    std::shared_ptr<const Source> sourceNot()
    {
        if (isOp(u"-") || isOp(u"!")) {
            next();
            auto n = std::make_shared<Source>();
            n->kind = Source::Kind::Not;
            n->a = sourceNot();
            return n;
        }
        return sourceAtom();
    }

    std::shared_ptr<const Source> sourceAtom()
    {
        const Token k = peek();
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
            QString path = k.text.trimmed();
            while (path.startsWith(u'/'))
                path.remove(0, 1);
            while (path.endsWith(u'/'))
                path.chop(1);
            out->value = path.isEmpty() ? QString() : paths::normalize(path);
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
            const Token l = peek();
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
    static QString linkTarget(const QString &inner)
    {
        QString target = inner.section(u'|', 0, 0);
        target = target.section(u'#', 0, 0).trimmed();
        return target;
    }

    // ---- expressions ----------------------------------------------------------------------------

    struct Depth {
        explicit Depth(Parser &p) : parser(p)
        {
            if (++parser.depth > MaxDepth)
                throw ParseError{messages::tooDeep(), parser.peek().line};
        }
        ~Depth() { --parser.depth; }
        Parser &parser;
    };

    std::shared_ptr<Expr> make(Expr::Kind kind, qsizetype from)
    {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        e->text = s.sliced(from, std::max<qsizetype>(0, lastEnd - from)).simplified();
        return e;
    }

    ExprPtr expression()
    {
        Depth d(*this);
        return orExpr();
    }

    ExprPtr binaryLoop(ExprPtr (Parser::*operand)(), std::initializer_list<QStringView> ops,
                       std::initializer_list<std::pair<QStringView, QStringView>> words)
    {
        const qsizetype from = peek().from;
        ExprPtr left = (this->*operand)();
        while (true) {
            QString op;
            for (const QStringView o : ops) {
                if (isOp(o)) {
                    op = o.toString();
                    break;
                }
            }
            if (op.isEmpty()) {
                for (const auto &[word, name] : words) {
                    if (isKeyword(peek(), word)) {
                        op = name.toString();
                        break;
                    }
                }
            }
            if (op.isEmpty())
                return left;
            next();
            if (op == u"==")
                op = u"="_s;
            else if (op == u"&&" || op == u"&")
                op = u"and"_s;
            else if (op == u"||" || op == u"|")
                op = u"or"_s;
            ExprPtr right = (this->*operand)();
            auto e = make(Expr::Kind::Binary, from);
            e->name = op;
            e->args = {left, right};
            left = e;
        }
    }

    ExprPtr orExpr() { return binaryLoop(&Parser::andExpr, {u"||", u"|"}, {{u"or", u"or"}}); }
    ExprPtr andExpr() { return binaryLoop(&Parser::compareExpr, {u"&&", u"&"}, {{u"and", u"and"}}); }
    ExprPtr compareExpr()
    {
        return binaryLoop(&Parser::addExpr, {u"!=", u"<=", u">=", u"==", u"=", u"<", u">"}, {});
    }
    ExprPtr addExpr() { return binaryLoop(&Parser::mulExpr, {u"+", u"-"}, {}); }
    ExprPtr mulExpr() { return binaryLoop(&Parser::unaryExpr, {u"*", u"/", u"%"}, {}); }

    ExprPtr unaryExpr()
    {
        Depth d(*this);
        const qsizetype from = peek().from;
        if (isOp(u"-") || isOp(u"!")) {
            const QString op = next().text;
            ExprPtr operand = unaryExpr();
            // -3 is a number.
            if (op == u"-" && operand->kind == Expr::Kind::Literal && operand->value.is(Value::Type::Number)) {
                auto e = make(Expr::Kind::Literal, from);
                e->value = Value::number(-operand->value.asNumber());
                return e;
            }
            auto e = make(Expr::Kind::Unary, from);
            e->name = op;
            e->args = {operand};
            return e;
        }
        return postfixExpr();
    }

    ExprPtr postfixExpr()
    {
        const qsizetype from = peek().from;
        ExprPtr e = primary();
        while (true) {
            if (isOp(u".")) {
                next();
                const Token k = peek();
                if (k.kind != Token::Kind::Ident)
                    throw ParseError{messages::expectedName(u"."_s), k.line};
                next();
                auto f = make(Expr::Kind::Field, from);
                f->name = canonicalKey(k.text);
                f->args = {e};
                e = f;
            } else if (isOp(u"[") && peek().kind == Token::Kind::Op) {
                next();
                ExprPtr index = expression();
                expectOp(u"]");
                auto f = make(Expr::Kind::Index, from);
                f->args = {e, index};
                e = f;
            } else if (isOp(u"(")) {
                if (e->kind != Expr::Kind::Variable)
                    throw ParseError{messages::unexpected(u"("_s), peek().line};
                next();
                QList<ExprPtr> args;
                if (!isOp(u")")) {
                    while (true) {
                        args.append(expression());
                        if (!isOp(u","))
                            break;
                        next();
                    }
                }
                expectOp(u")");
                auto c = make(Expr::Kind::Call, from);
                c->name = e->name;
                c->args = args;
                checkCall(*c, peek().line);
                e = c;
            } else {
                return e;
            }
        }
    }

    void checkCall(Expr &call, int line)
    {
        const QString name = call.name;
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
        Depth d(*this);
        const Token k = peek();
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
            QString inner = k.text;
            if (const qsizetype bar = inner.indexOf(u'|'); bar >= 0) {
                e->value = Value::string(inner.sliced(bar + 1).trimmed());
                inner = inner.first(bar);
            }
            if (const qsizetype hash = inner.indexOf(u'#'); hash >= 0) {
                e->raw = inner.sliced(hash + 1).trimmed();
                inner = inner.first(hash);
            }
            e->name = inner.trimmed();
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
            e->name = canonicalKey(k.text);
            // A function name keeps its spelling for messages; built-ins are found lower-case.
            if (isOp(u"("))
                e->name = k.text.toLower();
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
                e->args = {body};
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
                auto e = make(Expr::Kind::List, from);
                e->args = items;
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
    int depth = 0;
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
