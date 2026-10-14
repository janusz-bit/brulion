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

bool isUpperLetters(QStringView s, qsizetype maxLength)
{
    if (s.isEmpty() || s.size() > maxLength)
        return false;
    for (const QChar c : s) {
        if (c < u'A' || c > u'Z')
            return false;
    }
    return true;
}

/// `^[A-Z]{1,3}[1-9]\d{0,8}$` -> (row, column), both 0-based.
std::optional<std::pair<int, int>> parseCellRef(QStringView s)
{
    qsizetype letters = 0;
    while (letters < s.size() && s.at(letters) >= u'A' && s.at(letters) <= u'Z')
        ++letters;
    const QStringView digits = s.mid(letters);
    if (letters < 1 || letters > 3 || digits.isEmpty() || digits.size() > 9 || digits.front() == u'0')
        return std::nullopt;
    for (const QChar c : digits) {
        if (!detail::isAsciiDigit(c))
            return std::nullopt;
    }
    return std::pair<int, int>(digits.toInt() - 1, columnIndexOf(s.left(letters)));
}

std::optional<TableRef::Direction> directionWord(QStringView word)
{
    const QString lower = word.toString().toLower();
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

bool isReservedName(QStringView name)
{
    return isOperatorWord(name);
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
            if (m_options.statements && peek() && peek()->kind == TokenKind::Semicolon) {
                ++m_pos;
                continue;
            }
            if (!atEnd())
                failUnexpected(*peek());
            break;
        }
        program.refs = m_refs;
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
            if (peekOperator(u"=", 1)) {
                s.kind = Statement::Kind::Assignment;
                s.name = peek()->text;
                if (isReservedName(s.name)) {
                    fail(CalcErrorCode::CannotAssign, s.name);
                    return s;
                }
                m_pos += 2;
                s.expr = parseExpression(0);
                return s;
            }
            if (const std::optional<QStringList> params = functionDefinitionParams()) {
                s.kind = Statement::Kind::FunctionDefinition;
                s.name = peek()->text;
                s.params = *params;
                if (isReservedName(s.name)) {
                    fail(CalcErrorCode::CannotAssign, s.name);
                    return s;
                }
                m_pos += 3 + std::max<qsizetype>(0, 2 * params->size() - 1); // name ( params ) =
                ++m_pos;
                s.expr = parseExpression(0);
                return s;
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
        qsizetype i = 2;
        if (peekKind(TokenKind::RParen, i)) {
            ++i;
        } else {
            while (true) {
                const Token *p = peek(i);
                if (!p || p->kind != TokenKind::Ident)
                    return std::nullopt;
                params.append(p->text);
                ++i;
                if (peekKind(TokenKind::Comma, i) || peekKind(TokenKind::Semicolon, i)) {
                    ++i;
                    continue;
                }
                if (!peekKind(TokenKind::RParen, i))
                    return std::nullopt;
                ++i;
                break;
            }
        }
        if (!peekOperator(u"=", i))
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

    NodePtr parseInfix(NodePtr left, const InfixRule &rule)
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
            node->ops = ops;
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
        if (!peekOperator(u":")) {
            if (atEnd())
                return fail(CalcErrorCode::UnexpectedEnd);
            return failUnexpected(*peek());
        }
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

    NodePtr parseCall(const QString &name)
    {
        ++m_pos; // (
        m_ternaries.append(0);
        std::vector<NodePtr> args;
        if (peekKind(TokenKind::RParen)) {
            ++m_pos;
        } else {
            while (true) {
                NodePtr arg = parseExpression(0);
                if (!arg) {
                    m_ternaries.removeLast();
                    return nullptr;
                }
                args.push_back(std::move(arg));
                if (peekKind(TokenKind::Comma) || peekKind(TokenKind::Semicolon)) {
                    ++m_pos;
                    continue;
                }
                if (peekKind(TokenKind::RParen)) {
                    ++m_pos;
                    break;
                }
                m_ternaries.removeLast();
                if (atEnd())
                    return fail(CalcErrorCode::ParenExpected, QStringLiteral(")"));
                return failUnexpected(*peek());
            }
        }
        m_ternaries.removeLast();
        NodePtr node = make(NodeKind::Call, std::move(args));
        if (node)
            node->text = name;
        return node;
    }

    NodePtr parseBracket()
    {
        if (NodePtr header = parseHeaderRef())
            return header;
        ++m_pos; // [
        m_ternaries.append(0);
        std::vector<NodePtr> items;
        if (peekKind(TokenKind::RBracket)) {
            ++m_pos;
        } else {
            while (true) {
                NodePtr item = parseExpression(0);
                if (!item) {
                    m_ternaries.removeLast();
                    return nullptr;
                }
                items.push_back(std::move(item));
                if (peekKind(TokenKind::Comma) || peekKind(TokenKind::Semicolon)) {
                    ++m_pos;
                    continue;
                }
                if (peekKind(TokenKind::RBracket)) {
                    ++m_pos;
                    break;
                }
                m_ternaries.removeLast();
                if (atEnd())
                    return fail(CalcErrorCode::ParenExpected, QStringLiteral("]"));
                return failUnexpected(*peek());
            }
        }
        m_ternaries.removeLast();
        return make(NodeKind::List, std::move(items));
    }

    // --- table references -------------------------------------------------------------------

    NodePtr refNode(TableRef ref)
    {
        m_refs.append(std::move(ref));
        NodePtr node = makeLeaf(NodeKind::Ref, m_refs.last().text);
        if (node)
            node->ref = int(m_refs.size() - 1);
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
        if (const auto cell = parseCellRef(t.text)) {
            TableRef ref;
            if (rangeColonFollows()) {
                if (const auto end = parseCellRef(peek(2)->text)) {
                    ref.kind = TableRef::Kind::Range;
                    ref.row1 = cell->first;
                    ref.col1 = cell->second;
                    ref.row2 = end->first;
                    ref.col2 = end->second;
                    ref.text = t.text + u':' + peek(2)->text;
                    m_pos += 3;
                    return refNode(std::move(ref));
                }
            }
            ref.kind = TableRef::Kind::Cell;
            ref.row1 = ref.row2 = cell->first;
            ref.col1 = ref.col2 = cell->second;
            ref.text = t.text;
            ++m_pos;
            return refNode(std::move(ref));
        }
        if (isUpperLetters(t.text, 3) && rangeColonFollows() && isUpperLetters(peek(2)->text, 3)) {
            TableRef ref;
            ref.kind = TableRef::Kind::Range;
            ref.row1 = 0;
            ref.row2 = INT_MAX;
            ref.col1 = columnIndexOf(t.text);
            ref.col2 = columnIndexOf(peek(2)->text);
            ref.text = t.text + u':' + peek(2)->text;
            m_pos += 3;
            return refNode(std::move(ref));
        }
        if (const auto dir = directionWord(t.text)) {
            TableRef ref;
            ref.kind = TableRef::Kind::Direction;
            ref.direction = *dir;
            ref.text = t.text;
            ++m_pos;
            return refNode(std::move(ref));
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
        TableRef ref;
        ref.kind = TableRef::Kind::Header;
        ref.col1 = ref.col2 = match->column;
        ref.text = m_source.mid(open.pos, close.pos + close.length - open.pos).toString();
        m_pos = match->close + 1;
        return refNode(std::move(ref));
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
