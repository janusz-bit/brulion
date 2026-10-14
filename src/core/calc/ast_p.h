// Internal: abstract syntax tree of calc expressions (produced by parser_p.h, consumed by
// evaluator_p.h). Not part of the module's public API.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace brulion::calc {

enum class NodeKind {
    Number,         ///< `number`
    String,         ///< `text`
    Ident,          ///< `text` = name (variable, constant, unit, currency, aggregate word)
    Negate,         ///< -a
    Plus,           ///< +a
    Not,            ///< not a, !a
    Sqrt,           ///< √a
    Factorial,      ///< a!
    Percent,        ///< a%
    Add,
    Sub,
    Mul,
    ImplicitMul,    ///< juxtaposition: `5 km`, `2 pi`, `2(3)`
    Div,
    Mod,
    Pow,            ///< a ^ b, a ** b, a² (superscript: children[1] is a Number)
    And,
    Or,
    Xor,
    Compare,        ///< chained comparison: children = operands, `ops` = operators
    Ternary,        ///< cond ? a : b
    Call,           ///< `text` = function name, children = arguments
    List,           ///< [a, b, c]
    Convert,        ///< a to <unit expression>
    PercentOf,      ///< 20% of x (`text` = the word as written: of, z, od)
    AsPercentOf,    ///< x as % of y
    CurrencyPrefix, ///< `text` = symbol written before a number: $12, €5, PLN 12
    CurrencyUnit,   ///< `text` = symbol written alone/after a number: 12 €, $
    Ref,            ///< table reference, `ref` = index into ParsedProgram::refs
};

enum class CompareOp { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

struct Node;
using NodePtr = std::unique_ptr<Node>;

/// Left-associative binary operators whose chains (`1 + 2 - 3 + …`, `2 · 3 / 4`) are evaluated
/// iteratively along the left spine, so a long line of terms is not a deep recursion.
inline bool isLeftChain(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Add:
    case NodeKind::Sub:
    case NodeKind::Mul:
    case NodeKind::ImplicitMul:
    case NodeKind::Div:
    case NodeKind::Mod:
        return true;
    default:
        return false;
    }
}

struct Node {
    NodeKind kind = NodeKind::Number;
    double number = 0;
    QString text;
    std::vector<NodePtr> children;
    QList<CompareOp> ops;
    int ref = -1;
    /// Evaluation recursion depth of the subtree (bounded by the parser): like its height, but
    /// the left operand of a left-associative chain does not count (see isLeftChain()).
    int depth = 1;

    Node() = default;
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    /// Iterative: a chain of thousands of terms is thousands of nodes deep.
    ~Node()
    {
        std::vector<NodePtr> pending = std::move(children);
        while (!pending.empty()) {
            NodePtr node = std::move(pending.back());
            pending.pop_back();
            if (!node)
                continue;
            for (NodePtr &child : node->children)
                pending.push_back(std::move(child));
            node->children.clear();
        } // each `node` dies without children: no recursion
    }
};

/// A number literal, possibly signed (`5`, `-5`): the left side of a quantity ("5 a", "-2 a")
/// and of mathjs' "rule 2" (`1/2 kg`).
inline bool isNumberLiteral(const Node &node)
{
    if (node.kind == NodeKind::Negate || node.kind == NodeKind::Plus)
        return !node.children.empty() && node.children[0]->kind == NodeKind::Number;
    return node.kind == NodeKind::Number;
}

/// A user-defined function: `brutto(netto) = netto * 1,23`.
struct FunctionDef {
    QString name;
    QStringList params;
    NodePtr body;
};

} // namespace brulion::calc
