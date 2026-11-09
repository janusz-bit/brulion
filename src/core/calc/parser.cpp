#include "calc/parser_p.h"

#include "calc/numbers.h"
#include "calc/units.h"
#include "calc/util_p.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace brulion::calc {

namespace {

/// Deepest nesting of the recursive descent (brackets, unary chains, right-assoc. powers).
constexpr int kMaxParseDepth = 200;
/// Deepest AST allowed (Node::depth: bounds the evaluator's recursion; long left-associative
/// chains like `1+1+…` do not add to it).
constexpr int kMaxAstDepth = 500;

// Binding powers (higher binds tighter).
constexpr int kTernaryBp = 5;
constexpr int kOrBp = 10;
constexpr int kXorBp = 12;
constexpr int kAndBp = 14;
constexpr int kNotBp = 16;
constexpr int kCompareBp = 20;
constexpr int kConvertBp = 25;
constexpr int kAsPercentBp = 27;
constexpr int kAdditiveBp = 30;
constexpr int kMultiplicativeBp = 40;
constexpr int kImplicitBp = 45;
constexpr int kUnaryBp = 50;
constexpr int kPowerBp = 60;
constexpr int kPostfixBp = 70;

enum class Infix { None, Binary, Postfix, Compare, Ternary, Convert, PercentOf, AsPercentOf, Implicit };

struct InfixRule {
    Infix kind = Infix::None;
    NodeKind node = NodeKind::Add;
    int lbp = 0;
    bool rightAssoc = false;
};

std::optional<CompareOp> compareOp(const Token *t)
{
    if (!t || t->kind != TokenKind::Operator)
        return std::nullopt;
    if (t->text == u"==" || t->text == u"=")
        return CompareOp::Equal;
    if (t->text == u"!=")
        return CompareOp::NotEqual;
    if (t->text == u"<")
        return CompareOp::Less;
    if (t->text == u"<=")
        return CompareOp::LessEqual;
    if (t->text == u">")
        return CompareOp::Greater;
    if (t->text == u">=")
        return CompareOp::GreaterEqual;
    return std::nullopt;
}

bool isUpperLetter(QChar c)
{
    return c >= u'A' && c <= u'Z';
}

bool isUpperLetters(QStringView s, qsizetype maxLength)
{
    return !s.isEmpty() && s.size() <= maxLength && std::ranges::all_of(s, isUpperLetter);
}

/// `^[A-Z]{1,3}[1-9]\d{0,8}$` -> (row, column), both 0-based.
std::optional<std::pair<int, int>> parseCellRef(QStringView s)
{
    qsizetype letters = 0;
    while (letters < s.size() && isUpperLetter(s.at(letters)))
        ++letters;
    const QStringView digits = s.sliced(letters);
    if (letters < 1 || letters > 3 || digits.isEmpty() || digits.size() > 9 || digits.front() == u'0'
        || !std::ranges::all_of(digits, detail::isAsciiDigit))
        return std::nullopt;
    return std::pair(digits.toInt() - 1, columnIndexOf(s.first(letters)));
}

std::optional<TableRef::Direction> directionWord(const QString &word)
{
    const QString lower = word.toLower();
    if (lower == u"above" || lower == u"powyżej")
        return TableRef::Direction::Above;
    if (lower == u"below" || lower == u"poniżej")
        return TableRef::Direction::Below;
    if (lower == u"left")
        return TableRef::Direction::Left;
    if (lower == u"right")
        return TableRef::Direction::Right;
    return std::nullopt;
}

class Parser {
public:
    Parser(const QList<Token> &tokens, QStringView source, const ParseOptions &options)
        : m_tokens(tokens)
        , m_source(source)
        , m_options(options)
    {
    }

    ParsedProgram run()
    {
        ParsedProgram program;
        while (!atEnd() && !m_error) {
            Statement statement = parseStatement();
            if (m_error)
                break;
            program.statements.push_back(std::move(statement));
            if (m_options.statements && peekKind(TokenKind::Semicolon)) {
                ++m_pos;
                continue;
            }
            if (!atEnd())
                failUnexpected(*peek());
            break;
        }
        program.refs = std::move(m_refs);
        program.error = m_error;
        if (m_error)
            program.statements.clear();
        return program;
    }

private:
    // --- token access -------------------------------------------------------------------

    const Token *peek(qsizetype offset = 0) const
    {
        const qsizetype i = m_pos + offset;
        return i < m_tokens.size() ? &m_tokens.at(i) : nullptr;
    }
    bool atEnd() const { return m_pos >= m_tokens.size(); }
    bool peekKind(TokenKind kind, qsizetype offset = 0) const
    {
        const Token *t = peek(offset);
        return t && t->kind == kind;
    }
    bool peekOperator(QStringView op, qsizetype offset = 0) const
    {
        const Token *t = peek(offset);
        return t && t->isOperator(op);
    }
    QString sourceText(const Token &t) const { return m_source.mid(t.pos, t.length).toString(); }

    // --- errors ---------------------------------------------------------------------------

    NodePtr fail(CalcErrorCode code, const QString &arg = {})
    {
        if (!m_error)
            m_error = CalcError{code, arg};
        return nullptr;
    }

    NodePtr failUnexpected(const Token &t)
    {
        if (t.kind == TokenKind::Operator)
            return fail(CalcErrorCode::UnexpectedOperator, t.text);
        return fail(CalcErrorCode::UnexpectedPart, sourceText(t));
    }

    // --- node construction -------------------------------------------------------------------

    NodePtr make(NodeKind kind, std::vector<NodePtr> children = {})
    {
        auto node = std::make_unique<Node>();
        node->kind = kind;
        int depth = 0;
        for (const NodePtr &c : children)
            depth = std::max(depth, c->depth);
        node->depth = depth + 1;
        if (isLeftChain(kind) && children.size() == 2 && isLeftChain(children[0]->kind))
            node->depth = std::max(children[0]->depth, children[1]->depth + 1); // evaluated in a loop
        node->children = std::move(children);
        if (node->depth > kMaxAstDepth)
            return fail(CalcErrorCode::TooComplex);
        return node;
    }

    /// Never null: a leaf is never too deep.
    NodePtr makeLeaf(NodeKind kind, QString text = {}, double number = 0)
    {
        NodePtr node = make(kind);
        node->text = std::move(text);
        node->number = number;
        return node;
    }

    NodePtr makeUnary(NodeKind kind, NodePtr child)
    {
        if (!child)
            return nullptr;
        std::vector<NodePtr> children;
        children.push_back(std::move(child));
        return make(kind, std::move(children));
    }

    NodePtr makeBinary(NodeKind kind, NodePtr a, NodePtr b)
    {
        if (!a || !b)
            return nullptr;
        std::vector<NodePtr> children;
        children.push_back(std::move(a));
        children.push_back(std::move(b));
        return make(kind, std::move(children));
    }

    // --- statements -------------------------------------------------------------------

    Statement parseStatement()
    {
        Statement s;
        if (m_options.statements && peekKind(TokenKind::Ident)) {
            qsizetype head = 0; // tokens before the expression: `name =` or `name(params) =`
            if (peekOperator(u"=", 1)) {
                s.kind = Statement::Kind::Assignment;
                head = 2;
            } else if (std::optional<QStringList> params = functionDefinitionParams()) {
                s.kind = Statement::Kind::FunctionDefinition;
                head = 4 + std::max<qsizetype>(0, 2 * params->size() - 1); // name ( params ) =
                s.params = std::move(*params);
            }
            if (head > 0) {
                s.name = peek()->text;
                if (isOperatorWord(s.name)) {
                    fail(CalcErrorCode::CannotAssign, s.name);
                    return s;
                }
                m_pos += head;
            }
        }
        s.expr = parseExpression(0);
        return s;
    }

    /// `name(a, b) =` at the current position: the parameter names.
    std::optional<QStringList> functionDefinitionParams() const
    {
        if (!peekKind(TokenKind::LParen, 1))
            return std::nullopt;
        QStringList params;
        qsizetype i = 2; // after `name (`
        if (!peekKind(TokenKind::RParen, i)) {
            while (true) {
                if (!peekKind(TokenKind::Ident, i))
                    return std::nullopt;
                params.append(peek(i)->text);
                ++i;
                if (!peekKind(TokenKind::Comma, i) && !peekKind(TokenKind::Semicolon, i))
                    break;
                ++i;
            }
        }
        if (!peekKind(TokenKind::RParen, i) || !peekOperator(u"=", i + 1))
            return std::nullopt;
        return params;
    }

    // --- expressions ----------------------------------------------------------------------

    NodePtr parseExpression(int minBp)
    {
        if (++m_depth > kMaxParseDepth) {
            --m_depth;
            return fail(CalcErrorCode::TooComplex);
        }
        NodePtr left = parsePrefix();
        if (minBp < kImplicitBp)
            left = foldNumberDivisions(std::move(left));
        while (left && !m_error) {
            const InfixRule rule = infixRule();
            if (rule.kind == Infix::None || rule.lbp <= minBp)
                break;
            left = parseInfix(std::move(left), rule);
        }
        --m_depth;
        return m_error ? nullptr : std::move(left);
    }

    /// mathjs' "rule 2", as in the v1 engine: in `number / number factor` the division binds
    /// before the implicit multiplication, so `1/2 kg` is (1/2)·kg and `3/4 h` is 45 minutes,
    /// while `100 km / 2 h` stays (100 km)/(2 h). `left` is the operand just parsed.
    NodePtr foldNumberDivisions(NodePtr left)
    {
        const Node *last = left.get();
        while (left && isNumberLiteral(*last) && peekOperator(u"/") && peekKind(TokenKind::Number, 1)
               && infixRule(2).kind == Infix::Implicit) {
            ++m_pos; // '/'
            NodePtr divisor = parsePrefix();
            if (!divisor)
                return nullptr;
            last = divisor.get();
            left = makeBinary(NodeKind::Div, std::move(left), std::move(divisor));
        }
        return left;
    }

    bool startsOperand(const Token *t) const
    {
        if (!t)
            return false;
        switch (t->kind) {
        case TokenKind::Number:
        case TokenKind::String:
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::Currency:
            return true;
        case TokenKind::Ident:
            return !isOperatorWord(t->text);
        case TokenKind::Operator:
            return t->text == u"√";
        default:
            return false;
        }
    }

    /// A unit or currency name follows (`5 km in mi`, `na PLN`).
    bool startsUnit(const Token *t) const
    {
        if (!t)
            return false;
        if (t->kind == TokenKind::Currency)
            return true;
        return t->kind == TokenKind::Ident && (findQuantityUnit(t->text) || !currencyCode(t->text).isEmpty());
    }

    /// How the token `at` positions ahead continues an expression.
    InfixRule infixRule(qsizetype at = 0) const
    {
        const Token *t = peek(at);
        if (!t)
            return {};
        switch (t->kind) {
        case TokenKind::Operator:
            return operatorRule(*t, at);
        case TokenKind::Superscript:
            return {Infix::Postfix, NodeKind::Pow, kPostfixBp};
        case TokenKind::Ident:
            return wordRule(*t, at);
        case TokenKind::LParen:
        case TokenKind::Currency:
            return {Infix::Implicit, NodeKind::ImplicitMul, kImplicitBp};
        case TokenKind::LBracket: // `0,5 [Cena]` (a list literal is never a factor)
            if (headerAt(m_pos + at))
                return {Infix::Implicit, NodeKind::ImplicitMul, kImplicitBp};
            return {};
        default:
            return {};
        }
    }

    InfixRule operatorRule(const Token &t, qsizetype at) const
    {
        const QString &op = t.text;
        if (op == u"+")
            return {Infix::Binary, NodeKind::Add, kAdditiveBp};
        if (op == u"-")
            return {Infix::Binary, NodeKind::Sub, kAdditiveBp};
        if (op == u"*")
            return {Infix::Binary, NodeKind::Mul, kMultiplicativeBp};
        if (op == u"/")
            return {Infix::Binary, NodeKind::Div, kMultiplicativeBp};
        if (op == u"^")
            return {Infix::Binary, NodeKind::Pow, kPowerBp, true};
        if (op == u"%") {
            const Token *next = peek(at + 1);
            if (startsOperand(next) && !(next->kind == TokenKind::Ident && isOfWord(next->text)))
                return {Infix::Binary, NodeKind::Mod, kMultiplicativeBp};
            return {Infix::Postfix, NodeKind::Percent, kPostfixBp};
        }
        if (op == u"!")
            return {Infix::Postfix, NodeKind::Factorial, kPostfixBp};
        if (op == u"and")
            return {Infix::Binary, NodeKind::And, kAndBp};
        if (op == u"or")
            return {Infix::Binary, NodeKind::Or, kOrBp};
        if (compareOp(&t))
            return {Infix::Compare, NodeKind::Compare, kCompareBp};
        if (op == u"?")
            return {Infix::Ternary, NodeKind::Ternary, kTernaryBp, true};
        if (op == u"√")
            return {Infix::Implicit, NodeKind::ImplicitMul, kImplicitBp};
        return {};
    }

    InfixRule wordRule(const Token &t, qsizetype at) const
    {
        const QString lower = t.text.toLower();
        const InfixRule implicit{Infix::Implicit, NodeKind::ImplicitMul, kImplicitBp};
        if (lower == u"to")
            return {Infix::Convert, NodeKind::Convert, kConvertBp};
        if (lower == u"in" || t.text == u"na" || t.text == u"w")
            return startsUnit(peek(at + 1)) ? InfixRule{Infix::Convert, NodeKind::Convert, kConvertBp} : implicit;
        if (lower == u"mod")
            return {Infix::Binary, NodeKind::Mod, kMultiplicativeBp};
        if (lower == u"and")
            return {Infix::Binary, NodeKind::And, kAndBp};
        if (lower == u"or")
            return {Infix::Binary, NodeKind::Or, kOrBp};
        if (lower == u"xor")
            return {Infix::Binary, NodeKind::Xor, kXorBp};
        if (lower == u"not")
            return {};
        if (isOfWord(t.text)) {
            const Token *next = peek(at + 1);
            const bool operand = startsOperand(next) || (next && (next->isOperator(u"-") || next->isOperator(u"+")));
            return operand ? InfixRule{Infix::PercentOf, NodeKind::PercentOf, kMultiplicativeBp} : implicit;
        }
        if (lower == u"as" || t.text == u"jako") {
            const Token *of = peek(at + 2);
            if (peekOperator(u"%", at + 1) && of && of->kind == TokenKind::Ident && isOfWord(of->text))
                return {Infix::AsPercentOf, NodeKind::AsPercentOf, kAsPercentBp};
        }
        return implicit;
    }

    NodePtr parseInfix(NodePtr left, InfixRule rule)
    {
        switch (rule.kind) {
        case Infix::Binary: {
            ++m_pos;
            NodePtr right = parseExpression(rule.rightAssoc ? rule.lbp - 1 : rule.lbp);
            return makeBinary(rule.node, std::move(left), std::move(right));
        }
        case Infix::Postfix: {
            const Token &t = m_tokens.at(m_pos++);
            if (t.kind == TokenKind::Superscript)
                return makeBinary(NodeKind::Pow, std::move(left), makeLeaf(NodeKind::Number, {}, t.number));
            return makeUnary(rule.node, std::move(left));
        }
        case Infix::Compare:
            return parseComparison(std::move(left));
        case Infix::Ternary:
            return parseTernary(std::move(left));
        case Infix::Convert: {
            ++m_pos;
            return makeBinary(NodeKind::Convert, std::move(left), parseExpression(kConvertBp));
        }
        case Infix::PercentOf: {
            const QString word = sourceText(m_tokens.at(m_pos++)); // for the error of `5 z 10`
            return withText(makeBinary(NodeKind::PercentOf, std::move(left), parseExpression(kMultiplicativeBp)), word);
        }
        case Infix::AsPercentOf: {
            m_pos += 3; // as % of
            return makeBinary(NodeKind::AsPercentOf, std::move(left), parseExpression(kAsPercentBp));
        }
        case Infix::Implicit:
            return makeBinary(NodeKind::ImplicitMul, std::move(left), parseExpression(kImplicitBp));
        case Infix::None:
            break;
        }
        return left;
    }

    NodePtr parseComparison(NodePtr left)
    {
        std::vector<NodePtr> operands;
        QList<CompareOp> ops;
        operands.push_back(std::move(left));
        while (const std::optional<CompareOp> op = compareOp(peek())) {
            ++m_pos;
            NodePtr right = parseExpression(kCompareBp);
            if (!right)
                return nullptr;
            ops.append(*op);
            operands.push_back(std::move(right));
        }
        NodePtr node = make(NodeKind::Compare, std::move(operands));
        if (node)
            node->ops = std::move(ops);
        return node;
    }

    NodePtr parseTernary(NodePtr condition)
    {
        ++m_pos; // ?
        ++m_ternaries.last();
        NodePtr then = parseExpression(0);
        --m_ternaries.last();
        if (!then)
            return nullptr;
        if (!peekOperator(u":"))
            return atEnd() ? fail(CalcErrorCode::UnexpectedEnd) : failUnexpected(*peek());
        ++m_pos;
        NodePtr otherwise = parseExpression(kTernaryBp - 1);
        if (!otherwise)
            return nullptr;
        std::vector<NodePtr> children;
        children.push_back(std::move(condition));
        children.push_back(std::move(then));
        children.push_back(std::move(otherwise));
        return make(NodeKind::Ternary, std::move(children));
    }

    NodePtr parsePrefix()
    {
        const Token *t = peek();
        if (!t)
            return fail(CalcErrorCode::UnexpectedEnd);
        switch (t->kind) {
        case TokenKind::Number:
            ++m_pos;
            if (!std::isfinite(t->number))
                return fail(CalcErrorCode::TooBig);
            return makeLeaf(NodeKind::Number, t->text, t->number);
        case TokenKind::String:
            ++m_pos;
            if (t->unterminated)
                return fail(CalcErrorCode::UnterminatedString);
            return makeLeaf(NodeKind::String, t->text);
        case TokenKind::Ident:
            return parseIdentifier();
        case TokenKind::LParen:
            return parseGroup();
        case TokenKind::LBracket:
            return parseBracket();
        case TokenKind::Currency: {
            ++m_pos;
            const Token *next = peek();
            if (startsOperand(next) && next->kind != TokenKind::Currency)
                return withText(makeUnary(NodeKind::CurrencyPrefix, parseExpression(kImplicitBp)), t->text);
            return makeLeaf(NodeKind::CurrencyUnit, t->text);
        }
        case TokenKind::Operator:
            return parsePrefixOperator(*t);
        default:
            return failUnexpected(*t);
        }
    }

    static NodePtr withText(NodePtr node, const QString &text, double number = 0)
    {
        if (node) {
            node->text = text;
            node->number = number;
        }
        return node;
    }

    NodePtr parsePrefixOperator(const Token &t)
    {
        if (t.text == u"-" || t.text == u"+" || t.text == u"!") {
            ++m_pos;
            const NodeKind kind = t.text == u"-" ? NodeKind::Negate : t.text == u"+" ? NodeKind::Plus : NodeKind::Not;
            return makeUnary(kind, parseExpression(kUnaryBp));
        }
        if (t.text == u"√") {
            ++m_pos;
            return makeUnary(NodeKind::Sqrt, parseExpression(kPostfixBp));
        }
        return failUnexpected(t);
    }

    NodePtr parseGroup()
    {
        ++m_pos; // (
        m_ternaries.append(0);
        NodePtr inner = parseExpression(0);
        m_ternaries.removeLast();
        if (!inner)
            return nullptr;
        if (!peekKind(TokenKind::RParen))
            return fail(CalcErrorCode::ParenExpected, QStringLiteral(")"));
        ++m_pos;
        return inner;
    }

    NodePtr parseIdentifier()
    {
        const Token &t = *peek();
        if (m_options.tableRefs) {
            if (NodePtr ref = parseTableRef(); ref || m_error)
                return ref;
        }
        if (t.text.compare(u"not", Qt::CaseInsensitive) == 0) {
            ++m_pos;
            return makeUnary(NodeKind::Not, parseExpression(kNotBp));
        }
        if (peekKind(TokenKind::LParen, 1)) { // a call, also of operator words: mod(10, 3)
            ++m_pos;
            return parseCall(t.text);
        }
        if (isOperatorWord(t.text))
            return failUnexpected(t);
        if (!currencyCode(t.text).isEmpty() && peekKind(TokenKind::Number, 1)) {
            ++m_pos; // "PLN 12": prefix currency code (a variable of that name wins at evaluation)
            return withText(makeUnary(NodeKind::CurrencyPrefix, parseExpression(kImplicitBp)), t.text, 1);
        }
        ++m_pos;
        return makeLeaf(NodeKind::Ident, t.text);
    }

    /// The items of a call or list after its opening bracket: expressions separated by ',' or ';'
    /// up to the `close` bracket. Incomplete after an error (m_error).
    std::vector<NodePtr> parseItems(TokenKind close)
    {
        m_ternaries.append(0);
        std::vector<NodePtr> items;
        if (peekKind(close)) {
            ++m_pos;
        } else {
            while (NodePtr item = parseExpression(0)) {
                items.push_back(std::move(item));
                if (peekKind(TokenKind::Comma) || peekKind(TokenKind::Semicolon)) {
                    ++m_pos;
                    continue;
                }
                if (peekKind(close))
                    ++m_pos;
                else if (!atEnd())
                    failUnexpected(*peek());
                else if (close == TokenKind::RParen)
                    fail(CalcErrorCode::ParenExpected, QStringLiteral(")"));
                else
                    fail(CalcErrorCode::ParenExpected, QStringLiteral("]"));
                break;
            }
        }
        m_ternaries.removeLast();
        return items;
    }

    NodePtr parseCall(const QString &name)
    {
        const bool ambiguousComma = peek() && peek()->ambiguousComma;
        ++m_pos; // (
        std::vector<NodePtr> args = parseItems(TokenKind::RParen);
        NodePtr node = m_error ? nullptr : make(NodeKind::Call, std::move(args));
        if (node) {
            node->text = name;
            node->ambiguousComma = ambiguousComma;
        }
        return node;
    }

    NodePtr parseBracket()
    {
        if (NodePtr header = parseHeaderRef())
            return header;
        ++m_pos; // [
        std::vector<NodePtr> items = parseItems(TokenKind::RBracket);
        return m_error ? nullptr : make(NodeKind::List, std::move(items));
    }

    // --- table references -------------------------------------------------------------------

    NodePtr refNode(TableRef ref)
    {
        NodePtr node = makeLeaf(NodeKind::Ref, ref.text);
        node->ref = int(m_refs.size());
        m_refs.append(std::move(ref));
        return node;
    }

    bool rangeColonFollows() const
    {
        return peekOperator(u":", 1) && m_ternaries.last() == 0 && peekKind(TokenKind::Ident, 2);
    }

    /// A1, A1:B3, A:C, above/below/left/right at the current identifier; nullptr if none.
    NodePtr parseTableRef()
    {
        const Token &t = *peek();
        if (peekKind(TokenKind::LParen, 1))
            return nullptr; // a function call (LOG10(…))
        if (t.text == u"REF" && peekOperator(u"!", 1)) {
            m_pos += 2;
            return refNode({.kind = TableRef::Kind::Deleted, .text = t.text + u'!'});
        }
        if (const auto cell = parseCellRef(t.text)) {
            const Token *last = rangeColonFollows() ? peek(2) : nullptr;
            if (const auto end = last ? parseCellRef(last->text) : std::nullopt) {
                m_pos += 3;
                return refNode({.kind = TableRef::Kind::Range,
                                .row1 = cell->first,
                                .col1 = cell->second,
                                .row2 = end->first,
                                .col2 = end->second,
                                .text = t.text + u':' + last->text,
                                .pos = t.pos,
                                .length = last->pos + last->length - t.pos});
            }
            ++m_pos;
            return refNode({.kind = TableRef::Kind::Cell,
                            .row1 = cell->first,
                            .col1 = cell->second,
                            .row2 = cell->first,
                            .col2 = cell->second,
                            .text = t.text,
                            .pos = t.pos,
                            .length = t.length});
        }
        if (isUpperLetters(t.text, 3) && rangeColonFollows() && isUpperLetters(peek(2)->text, 3)) {
            const Token &last = *peek(2);
            m_pos += 3;
            return refNode({.kind = TableRef::Kind::Range,
                            .col1 = columnIndexOf(t.text),
                            .row2 = INT_MAX,
                            .col2 = columnIndexOf(last.text),
                            .text = t.text + u':' + last.text,
                            .pos = t.pos,
                            .length = last.pos + last.length - t.pos});
        }
        if (const auto dir = directionWord(t.text)) {
            ++m_pos;
            return refNode({.kind = TableRef::Kind::Direction, .direction = *dir, .text = t.text});
        }
        return nullptr;
    }

    struct HeaderMatch {
        qsizetype close = 0; ///< token index of the closing ']'
        int column = 0;
    };

    /// The `[` at token index `open` starts a [Header] naming a column (headers may contain
    /// brackets: "[Ilość [szt]]"); nullopt for a list literal or outside tables.
    std::optional<HeaderMatch> headerAt(qsizetype open) const
    {
        if (!m_options.tableRefs)
            return std::nullopt;
        int depth = 0;
        for (qsizetype i = open; i < m_tokens.size(); ++i) {
            const TokenKind kind = m_tokens.at(i).kind;
            if (kind == TokenKind::LBracket) {
                ++depth;
            } else if (kind == TokenKind::RBracket && --depth == 0) {
                const Token &first = m_tokens.at(open);
                const qsizetype from = first.pos + first.length;
                const QString name = normalizeHeader(m_source.mid(from, m_tokens.at(i).pos - from));
                const qsizetype column = name.isEmpty() ? -1 : m_options.headers.indexOf(name);
                if (column < 0)
                    return std::nullopt;
                return HeaderMatch{i, int(column)};
            }
        }
        return std::nullopt;
    }

    /// [Header] at the current token, or nullptr.
    NodePtr parseHeaderRef()
    {
        const std::optional<HeaderMatch> match = headerAt(m_pos);
        if (!match)
            return nullptr;
        const Token &open = m_tokens.at(m_pos);
        const Token &close = m_tokens.at(match->close);
        m_pos = match->close + 1;
        return refNode({.kind = TableRef::Kind::Header,
                        .col1 = match->column,
                        .col2 = match->column,
                        .text = m_source.mid(open.pos, close.pos + close.length - open.pos).toString()});
    }

    const QList<Token> &m_tokens;
    QStringView m_source;
    const ParseOptions &m_options;
    qsizetype m_pos = 0;
    int m_depth = 0;
    QList<int> m_ternaries{0}; ///< open `?` per bracket level: their ':' is not a range
    QList<TableRef> m_refs;
    CalcError m_error;
};

} // namespace

ParsedProgram parseProgram(const QList<Token> &tokens, QStringView source, const ParseOptions &options)
{
    return Parser(tokens, source, options).run();
}

QString normalizeHeader(QStringView header)
{
    const QString stripped = detail::toNfc(stripInlineMarkup(header));
    return stripped.simplified().toLower();
}

int columnIndexOf(QStringView letters)
{
    int n = 0;
    for (const QChar c : letters)
        n = n * 26 + (c.toUpper().unicode() - u'A' + 1);
    return n - 1;
}

} // namespace brulion::calc
