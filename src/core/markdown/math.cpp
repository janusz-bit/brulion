#include "markdown/math.h"

#include "markdown/lines_p.h" // isAsciiPunct

#include <QHash>
#include <QScopeGuard>
#include <QStringList>

namespace brulion::markdown {

using namespace Qt::StringLiterals;
using namespace detail;

namespace {

// --- formula writer ----------------------------------------------------------------------------
//
// A formula becomes a list of nodes; assemble() puts them together with the spacing of TeX
// (a space around binary operators and relations, none before punctuation or inside
// parentheses) and appends the ^ / _ scripts of every node as <sub> / <sup>.

enum class Kind { Atom, Bin, Rel, Punct, Open, Close, Func, Big };

struct Node {
    QString html;
    Kind kind = Kind::Atom;
    QString sub;
    QString sup;
};

QString escaped(QStringView s)
{
    return s.toString().toHtmlEscaped();
}

/// Space between two neighbouring nodes: TeX's medium space around Bin/Rel, none around
/// parentheses, a thin-ish space after punctuation and functions.
bool needsSpace(Kind prev, Kind cur)
{
    if (cur == Kind::Bin || cur == Kind::Rel)
        return prev != Kind::Open && prev != Kind::Bin && prev != Kind::Rel && prev != Kind::Punct
               && prev != Kind::Func;
    if (prev == Kind::Bin || prev == Kind::Rel)
        return cur != Kind::Close && cur != Kind::Punct;
    if (prev == Kind::Punct)
        return true;
    // A big operator keeps a space after its limits: "∑ᵢ₌₁ⁿ i", "∫₀¹ x dx".
    if (prev == Kind::Big)
        return cur == Kind::Atom || cur == Kind::Func;
    if (prev == Kind::Func)
        return cur != Kind::Open && cur != Kind::Close;
    if (cur == Kind::Func)
        return prev == Kind::Atom || prev == Kind::Close || prev == Kind::Big;
    return false;
}

QString assemble(const QList<Node> &nodes)
{
    QString out;
    bool first = true;
    Kind prev = Kind::Atom;
    for (const Node &n : nodes) {
        if (n.html.isEmpty() && n.sub.isEmpty() && n.sup.isEmpty())
            continue;
        if (!first && needsSpace(prev, n.kind))
            out += u' ';
        out += n.html;
        if (!n.sub.isEmpty())
            out += u"<sub>"_s + n.sub + u"</sub>"_s;
        if (!n.sup.isEmpty())
            out += u"<sup>"_s + n.sup + u"</sup>"_s;
        prev = n.kind;
        first = false;
    }
    while (out.startsWith(u' '))
        out.remove(0, 1);
    while (out.endsWith(u' '))
        out.chop(1);
    while (out.contains(u"  "_s))
        out.replace(u"  "_s, u" "_s);
    return out;
}

void push(QList<Node> &nodes, const QString &html, Kind kind)
{
    nodes.append(Node{html, kind, {}, {}});
}

/// The number of characters `html` shows: a tag or an entity counts as one character. Decides
/// how much room an argument of \frac has in a superscript / subscript.
qsizetype visibleLength(const QString &html)
{
    qsizetype n = 0;
    for (qsizetype i = 0; i < html.size();) {
        if (html[i] == u'<') {
            const qsizetype close = html.indexOf(u'>', i);
            i = close < 0 ? html.size() : close + 1;
            continue;
        }
        if (html[i] == u'&') {
            const qsizetype semi = html.indexOf(u';', i);
            i = semi < 0 ? html.size() : semi + 1;
            ++n;
            continue;
        }
        ++i;
        ++n;
    }
    return n;
}

struct Arg {
    QString html;
    bool simple = true;
};

/// Formulas nested deeper than this (braces, \frac, \sqrt, ^{…}, environments) show the rest as
/// their source. A level takes up to about 3 KB of stack: the limit keeps a formula far below the
/// 1 MB of Android's GUI thread, and real formulas stay well within it.
constexpr int MaxMathDepth = 48;

/// An argument that fits in a superscript / subscript: no scripts, decorations or line breaks of
/// its own and at most a few characters ("a", "12", "n+1"; not "<i>x</i><sup>2</sup>").
constexpr qsizetype MaxScriptChars = 4;

bool scriptable(const Arg &arg)
{
    if (!arg.simple)
        return false;
    return !arg.html.contains(u"<sup") && !arg.html.contains(u"<sub") && !arg.html.contains(u"<span")
           && !arg.html.contains(u"<br") && visibleLength(arg.html) <= MaxScriptChars;
}

QList<Node> parseRange(QStringView s, bool upright, bool display);
Arg parseArg(QStringView s, qsizetype &i, bool display);

/// Position of the `}` matching the `{` at `open`; -1 when the braces are unbalanced.
qsizetype matchingBrace(QStringView s, qsizetype open)
{
    int depth = 0;
    for (qsizetype i = open; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == u'\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
            ++i;
            continue;
        }
        if (c == u'{')
            ++depth;
        else if (c == u'}' && --depth == 0)
            return i;
    }
    return -1;
}

Arg parseArg(QStringView s, qsizetype &i, bool display)
{
    while (i < s.size() && s[i].isSpace())
        ++i;
    Arg a;
    if (i >= s.size()) {
        a.simple = false;
        return a;
    }
    if (s[i] == u'{') {
        const qsizetype close = matchingBrace(s, i);
        if (close < 0) {
            a.html = escaped(s.sliced(i + 1));
            a.simple = false;
            i = s.size();
            return a;
        }
        a.html = assemble(parseRange(s.sliced(i + 1, close - i - 1), false, display));
        i = close + 1;
    } else if (s[i] == u'\\') {
        const qsizetype start = i;
        ++i;
        if (i < s.size() && s[i].isLetter()) {
            while (i < s.size() && s[i].isLetter())
                ++i;
        } else if (i < s.size()) {
            ++i;
        }
        a.html = assemble(parseRange(s.sliced(start, i - start), false, display));
    } else {
        a.html = assemble(parseRange(s.sliced(i, 1), false, display));
        ++i;
    }
    return a;
}

void attachScript(QList<Node> &nodes, QChar which, const QString &html)
{
    if (html.isEmpty())
        return;
    if (nodes.isEmpty())
        nodes.append(Node{});
    Node &n = nodes.last();
    if (which == u'^') {
        n.sup.isEmpty() ? n.sup = html : n.sup += html;
    } else {
        n.sub.isEmpty() ? n.sub = html : n.sub += html;
    }
}

/// Puts the combining mark of an accent on the last character of `html` (inside its tags:
/// `<i>x</i>` + ̂ -> `<i>x̂</i>`).
QString withCombining(QString html, QChar mark)
{
    if (html.endsWith(u'>')) {
        const qsizetype open = html.lastIndexOf(u'<');
        if (open >= 0 && html[open + 1] == u'/')
            return html.left(open) + mark + html.mid(open);
    }
    return html + mark;
}

// --- symbols -----------------------------------------------------------------------------------

struct Symbol {
    QString text;
    Kind kind = Kind::Atom;
};

const QHash<QString, Symbol> &symbols()
{
    static const QHash<QString, Symbol> table = [] {
        QHash<QString, Symbol> t;
        const auto s = [&t](QStringView name, QStringView text, Kind kind = Kind::Atom) {
            t.insert(name.toString(), Symbol{text.toString(), kind});
        };
        // Lowercase Greek letters: italic, as TeX sets them (uppercase ones are upright).
        const auto greek = [&t](QStringView name, QStringView text) {
            t.insert(name.toString(), Symbol{u"<i>"_s + text.toString() + u"</i>"_s, Kind::Atom});
        };
        // Greek letters.
        greek(u"alpha"_s, u"α"_s);
        greek(u"beta"_s, u"β"_s);
        greek(u"gamma"_s, u"γ"_s);
        greek(u"delta"_s, u"δ"_s);
        greek(u"epsilon"_s, u"ϵ"_s);
        greek(u"varepsilon"_s, u"ε"_s);
        greek(u"zeta"_s, u"ζ"_s);
        greek(u"eta"_s, u"η"_s);
        greek(u"theta"_s, u"θ"_s);
        greek(u"vartheta"_s, u"ϑ"_s);
        greek(u"iota"_s, u"ι"_s);
        greek(u"kappa"_s, u"κ"_s);
        greek(u"varkappa"_s, u"ϰ"_s);
        greek(u"lambda"_s, u"λ"_s);
        greek(u"mu"_s, u"μ"_s);
        greek(u"nu"_s, u"ν"_s);
        greek(u"xi"_s, u"ξ"_s);
        greek(u"omicron"_s, u"ο"_s);
        greek(u"pi"_s, u"π"_s);
        greek(u"varpi"_s, u"ϖ"_s);
        greek(u"rho"_s, u"ρ"_s);
        greek(u"varrho"_s, u"ϱ"_s);
        greek(u"sigma"_s, u"σ"_s);
        greek(u"varsigma"_s, u"ς"_s);
        greek(u"tau"_s, u"τ"_s);
        greek(u"upsilon"_s, u"υ"_s);
        greek(u"phi"_s, u"φ"_s);
        greek(u"varphi"_s, u"ϕ"_s);
        greek(u"chi"_s, u"χ"_s);
        greek(u"psi"_s, u"ψ"_s);
        greek(u"omega"_s, u"ω"_s);
        greek(u"digamma"_s, u"ϝ"_s);
        s(u"Gamma"_s, u"Γ"_s);
        s(u"Delta"_s, u"Δ"_s);
        s(u"Theta"_s, u"Θ"_s);
        s(u"Lambda"_s, u"Λ"_s);
        s(u"Xi"_s, u"Ξ"_s);
        s(u"Pi"_s, u"Π"_s);
        s(u"Sigma"_s, u"Σ"_s);
        s(u"Upsilon"_s, u"Υ"_s);
        s(u"Phi"_s, u"Φ"_s);
        s(u"Psi"_s, u"Ψ"_s);
        s(u"Omega"_s, u"Ω"_s);

        // Binary operators.
        s(u"pm"_s, u"±"_s, Kind::Bin);
        s(u"mp"_s, u"∓"_s, Kind::Bin);
        s(u"times"_s, u"×"_s, Kind::Bin);
        s(u"div"_s, u"÷"_s, Kind::Bin);
        s(u"cdot"_s, u"⋅"_s, Kind::Bin);
        s(u"ast"_s, u"∗"_s, Kind::Bin);
        s(u"star"_s, u"⋆"_s, Kind::Bin);
        s(u"circ"_s, u"∘"_s, Kind::Bin);
        s(u"bullet"_s, u"∙"_s, Kind::Bin);
        s(u"oplus"_s, u"⊕"_s, Kind::Bin);
        s(u"ominus"_s, u"⊖"_s, Kind::Bin);
        s(u"otimes"_s, u"⊗"_s, Kind::Bin);
        s(u"oslash"_s, u"⊘"_s, Kind::Bin);
        s(u"odot"_s, u"⊙"_s, Kind::Bin);
        s(u"cap"_s, u"∩"_s, Kind::Bin);
        s(u"cup"_s, u"∪"_s, Kind::Bin);
        s(u"sqcap"_s, u"⊓"_s, Kind::Bin);
        s(u"sqcup"_s, u"⊔"_s, Kind::Bin);
        s(u"vee"_s, u"∨"_s, Kind::Bin);
        s(u"wedge"_s, u"∧"_s, Kind::Bin);
        s(u"land"_s, u"∧"_s, Kind::Bin);
        s(u"lor"_s, u"∨"_s, Kind::Bin);
        s(u"setminus"_s, u"∖"_s, Kind::Bin);
        s(u"uplus"_s, u"⊎"_s, Kind::Bin);
        s(u"dagger"_s, u"†"_s, Kind::Bin);
        s(u"ddagger"_s, u"‡"_s, Kind::Bin);
        s(u"amalg"_s, u"⨿"_s, Kind::Bin);
        s(u"wr"_s, u"≀"_s, Kind::Bin);
        s(u"diamond"_s, u"⋄"_s, Kind::Bin);
        s(u"dotplus"_s, u"∔"_s, Kind::Bin);
        s(u"ltimes"_s, u"⋉"_s, Kind::Bin);
        s(u"rtimes"_s, u"⋊"_s, Kind::Bin);
        s(u"boxplus"_s, u"⊞"_s, Kind::Bin);
        s(u"boxminus"_s, u"⊟"_s, Kind::Bin);
        s(u"boxtimes"_s, u"⊠"_s, Kind::Bin);
        s(u"boxdot"_s, u"⊡"_s, Kind::Bin);
        s(u"bmod"_s, u"mod"_s, Kind::Bin);

        // Relations.
        s(u"le"_s, u"≤"_s, Kind::Rel);
        s(u"leq"_s, u"≤"_s, Kind::Rel);
        s(u"ge"_s, u"≥"_s, Kind::Rel);
        s(u"geq"_s, u"≥"_s, Kind::Rel);
        s(u"ne"_s, u"≠"_s, Kind::Rel);
        s(u"neq"_s, u"≠"_s, Kind::Rel);
        s(u"equiv"_s, u"≡"_s, Kind::Rel);
        s(u"sim"_s, u"∼"_s, Kind::Rel);
        s(u"simeq"_s, u"≃"_s, Kind::Rel);
        s(u"approx"_s, u"≈"_s, Kind::Rel);
        s(u"cong"_s, u"≅"_s, Kind::Rel);
        s(u"propto"_s, u"∝"_s, Kind::Rel);
        s(u"parallel"_s, u"∥"_s, Kind::Rel);
        s(u"perp"_s, u"⊥"_s, Kind::Rel);
        s(u"mid"_s, u"∣"_s, Kind::Rel);
        s(u"nmid"_s, u"∤"_s, Kind::Rel);
        s(u"in"_s, u"∈"_s, Kind::Rel);
        s(u"notin"_s, u"∉"_s, Kind::Rel);
        s(u"ni"_s, u"∋"_s, Kind::Rel);
        s(u"owns"_s, u"∋"_s, Kind::Rel);
        s(u"subset"_s, u"⊂"_s, Kind::Rel);
        s(u"supset"_s, u"⊃"_s, Kind::Rel);
        s(u"subseteq"_s, u"⊆"_s, Kind::Rel);
        s(u"supseteq"_s, u"⊇"_s, Kind::Rel);
        s(u"nsubseteq"_s, u"⊈"_s, Kind::Rel);
        s(u"nsupseteq"_s, u"⊉"_s, Kind::Rel);
        s(u"sqsubset"_s, u"⊏"_s, Kind::Rel);
        s(u"sqsupset"_s, u"⊐"_s, Kind::Rel);
        s(u"sqsubseteq"_s, u"⊑"_s, Kind::Rel);
        s(u"sqsupseteq"_s, u"⊒"_s, Kind::Rel);
        s(u"prec"_s, u"≺"_s, Kind::Rel);
        s(u"succ"_s, u"≻"_s, Kind::Rel);
        s(u"preceq"_s, u"⪯"_s, Kind::Rel);
        s(u"succeq"_s, u"⪰"_s, Kind::Rel);
        s(u"ll"_s, u"≪"_s, Kind::Rel);
        s(u"gg"_s, u"≫"_s, Kind::Rel);
        s(u"asymp"_s, u"≍"_s, Kind::Rel);
        s(u"bowtie"_s, u"⋈"_s, Kind::Rel);
        s(u"models"_s, u"⊨"_s, Kind::Rel);
        s(u"vdash"_s, u"⊢"_s, Kind::Rel);
        s(u"dashv"_s, u"⊣"_s, Kind::Rel);
        s(u"doteq"_s, u"≐"_s, Kind::Rel);
        s(u"frown"_s, u"⌢"_s, Kind::Rel);
        s(u"smile"_s, u"⌣"_s, Kind::Rel);
        s(u"lesssim"_s, u"≲"_s, Kind::Rel);
        s(u"gtrsim"_s, u"≳"_s, Kind::Rel);
        s(u"trianglelefteq"_s, u"⊴"_s, Kind::Rel);
        s(u"trianglerighteq"_s, u"⊵"_s, Kind::Rel);
        s(u"colon"_s, u":"_s, Kind::Rel);

        // Arrows.
        s(u"leftarrow"_s, u"←"_s, Kind::Rel);
        s(u"gets"_s, u"←"_s, Kind::Rel);
        s(u"rightarrow"_s, u"→"_s, Kind::Rel);
        s(u"to"_s, u"→"_s, Kind::Rel);
        s(u"leftrightarrow"_s, u"↔"_s, Kind::Rel);
        s(u"Leftarrow"_s, u"⇐"_s, Kind::Rel);
        s(u"Rightarrow"_s, u"⇒"_s, Kind::Rel);
        s(u"Leftrightarrow"_s, u"⇔"_s, Kind::Rel);
        s(u"implies"_s, u"⟹"_s, Kind::Rel);
        s(u"iff"_s, u"⟺"_s, Kind::Rel);
        s(u"longleftarrow"_s, u"⟵"_s, Kind::Rel);
        s(u"longrightarrow"_s, u"⟶"_s, Kind::Rel);
        s(u"longleftrightarrow"_s, u"⟷"_s, Kind::Rel);
        s(u"Longleftarrow"_s, u"⟸"_s, Kind::Rel);
        s(u"Longrightarrow"_s, u"⟹"_s, Kind::Rel);
        s(u"Longleftrightarrow"_s, u"⟺"_s, Kind::Rel);
        s(u"uparrow"_s, u"↑"_s, Kind::Rel);
        s(u"downarrow"_s, u"↓"_s, Kind::Rel);
        s(u"updownarrow"_s, u"↕"_s, Kind::Rel);
        s(u"Uparrow"_s, u"⇑"_s, Kind::Rel);
        s(u"Downarrow"_s, u"⇓"_s, Kind::Rel);
        s(u"Updownarrow"_s, u"⇕"_s, Kind::Rel);
        s(u"mapsto"_s, u"↦"_s, Kind::Rel);
        s(u"longmapsto"_s, u"⟼"_s, Kind::Rel);
        s(u"hookleftarrow"_s, u"↩"_s, Kind::Rel);
        s(u"hookrightarrow"_s, u"↪"_s, Kind::Rel);
        s(u"leadsto"_s, u"⇝"_s, Kind::Rel);
        s(u"rightleftharpoons"_s, u"⇌"_s, Kind::Rel);
        s(u"leftrightharpoons"_s, u"⇋"_s, Kind::Rel);
        s(u"twoheadrightarrow"_s, u"↠"_s, Kind::Rel);
        s(u"twoheadleftarrow"_s, u"↞"_s, Kind::Rel);
        s(u"Rrightarrow"_s, u"⇛"_s, Kind::Rel);
        s(u"Lleftarrow"_s, u"⇚"_s, Kind::Rel);
        s(u"curvearrowright"_s, u"↷"_s, Kind::Rel);
        s(u"curvearrowleft"_s, u"↶"_s, Kind::Rel);
        s(u"circlearrowright"_s, u"↻"_s, Kind::Rel);
        s(u"circlearrowleft"_s, u"↺"_s, Kind::Rel);
        s(u"nwarrow"_s, u"↖"_s, Kind::Rel);
        s(u"nearrow"_s, u"↗"_s, Kind::Rel);
        s(u"searrow"_s, u"↘"_s, Kind::Rel);
        s(u"swarrow"_s, u"↙"_s, Kind::Rel);

        // Big operators.
        s(u"sum"_s, u"∑"_s, Kind::Big);
        s(u"prod"_s, u"∏"_s, Kind::Big);
        s(u"coprod"_s, u"∐"_s, Kind::Big);
        s(u"int"_s, u"∫"_s, Kind::Big);
        s(u"iint"_s, u"∬"_s, Kind::Big);
        s(u"iiint"_s, u"∭"_s, Kind::Big);
        s(u"oint"_s, u"∮"_s, Kind::Big);
        s(u"smallint"_s, u"∫"_s, Kind::Big);
        s(u"bigcup"_s, u"⋃"_s, Kind::Big);
        s(u"bigcap"_s, u"⋂"_s, Kind::Big);
        s(u"bigvee"_s, u"⋁"_s, Kind::Big);
        s(u"bigwedge"_s, u"⋀"_s, Kind::Big);
        s(u"bigoplus"_s, u"⨁"_s, Kind::Big);
        s(u"bigotimes"_s, u"⨂"_s, Kind::Big);
        s(u"bigodot"_s, u"⨀"_s, Kind::Big);
        s(u"biguplus"_s, u"⨄"_s, Kind::Big);
        s(u"bigsqcup"_s, u"⨆"_s, Kind::Big);

        // Function names (upright, a thin space before their argument).
        s(u"sin"_s, u"sin"_s, Kind::Func);
        s(u"cos"_s, u"cos"_s, Kind::Func);
        s(u"tan"_s, u"tan"_s, Kind::Func);
        s(u"cot"_s, u"cot"_s, Kind::Func);
        s(u"sec"_s, u"sec"_s, Kind::Func);
        s(u"csc"_s, u"csc"_s, Kind::Func);
        s(u"sinh"_s, u"sinh"_s, Kind::Func);
        s(u"cosh"_s, u"cosh"_s, Kind::Func);
        s(u"tanh"_s, u"tanh"_s, Kind::Func);
        s(u"coth"_s, u"coth"_s, Kind::Func);
        s(u"arcsin"_s, u"arcsin"_s, Kind::Func);
        s(u"arccos"_s, u"arccos"_s, Kind::Func);
        s(u"arctan"_s, u"arctan"_s, Kind::Func);
        s(u"arg"_s, u"arg"_s, Kind::Func);
        s(u"deg"_s, u"deg"_s, Kind::Func);
        s(u"dim"_s, u"dim"_s, Kind::Func);
        s(u"exp"_s, u"exp"_s, Kind::Func);
        s(u"hom"_s, u"hom"_s, Kind::Func);
        s(u"ker"_s, u"ker"_s, Kind::Func);
        s(u"lg"_s, u"lg"_s, Kind::Func);
        s(u"ln"_s, u"ln"_s, Kind::Func);
        s(u"log"_s, u"log"_s, Kind::Func);
        s(u"max"_s, u"max"_s, Kind::Func);
        s(u"min"_s, u"min"_s, Kind::Func);
        s(u"Pr"_s, u"Pr"_s, Kind::Func);
        s(u"sup"_s, u"sup"_s, Kind::Func);
        s(u"inf"_s, u"inf"_s, Kind::Func);
        s(u"lim"_s, u"lim"_s, Kind::Func);
        s(u"limsup"_s, u"lim sup"_s, Kind::Func);
        s(u"liminf"_s, u"lim inf"_s, Kind::Func);
        s(u"det"_s, u"det"_s, Kind::Func);
        s(u"gcd"_s, u"gcd"_s, Kind::Func);
        s(u"mod"_s, u"mod"_s, Kind::Func);

        // Delimiters.
        s(u"lbrace"_s, u"{"_s, Kind::Open);
        s(u"rbrace"_s, u"}"_s, Kind::Close);
        s(u"langle"_s, u"⟨"_s, Kind::Open);
        s(u"rangle"_s, u"⟩"_s, Kind::Close);
        s(u"lfloor"_s, u"⌊"_s, Kind::Open);
        s(u"rfloor"_s, u"⌋"_s, Kind::Close);
        s(u"lceil"_s, u"⌈"_s, Kind::Open);
        s(u"rceil"_s, u"⌉"_s, Kind::Close);
        s(u"lgroup"_s, u"⟮"_s, Kind::Open);
        s(u"rgroup"_s, u"⟯"_s, Kind::Close);
        s(u"lmoustache"_s, u"⎰"_s, Kind::Open);
        s(u"rmoustache"_s, u"⎱"_s, Kind::Close);
        s(u"lvert"_s, u"|"_s, Kind::Open);
        s(u"rvert"_s, u"|"_s, Kind::Close);
        s(u"lVert"_s, u"‖"_s, Kind::Open);
        s(u"rVert"_s, u"‖"_s, Kind::Close);
        s(u"vert"_s, u"|"_s);
        s(u"Vert"_s, u"‖"_s);
        s(u"backslash"_s, u"\\"_s);

        // Symbols.
        s(u"infty"_s, u"∞"_s);
        s(u"partial"_s, u"∂"_s);
        s(u"nabla"_s, u"∇"_s);
        s(u"forall"_s, u"∀"_s);
        s(u"exists"_s, u"∃"_s);
        s(u"nexists"_s, u"∄"_s);
        s(u"emptyset"_s, u"∅"_s);
        s(u"varnothing"_s, u"∅"_s);
        s(u"neg"_s, u"¬"_s);
        s(u"lnot"_s, u"¬"_s);
        s(u"top"_s, u"⊤"_s);
        s(u"bot"_s, u"⊥"_s);
        s(u"angle"_s, u"∠"_s);
        s(u"measuredangle"_s, u"∡"_s);
        s(u"triangle"_s, u"△"_s);
        s(u"square"_s, u"□"_s);
        s(u"Box"_s, u"□"_s);
        s(u"Diamond"_s, u"◇"_s);
        s(u"clubsuit"_s, u"♣"_s);
        s(u"diamondsuit"_s, u"♢"_s);
        s(u"heartsuit"_s, u"♥"_s);
        s(u"spadesuit"_s, u"♠"_s);
        s(u"flat"_s, u"♭"_s);
        s(u"natural"_s, u"♮"_s);
        s(u"sharp"_s, u"♯"_s);
        s(u"checkmark"_s, u"✓"_s);
        s(u"dag"_s, u"†"_s);
        s(u"prime"_s, u"′"_s);
        s(u"backprime"_s, u"‵"_s);
        s(u"ell"_s, u"ℓ"_s);
        s(u"hbar"_s, u"ℏ"_s);
        s(u"hslash"_s, u"ℏ"_s);
        s(u"imath"_s, u"ı"_s);
        s(u"jmath"_s, u"ȷ"_s);
        s(u"Re"_s, u"ℜ"_s);
        s(u"Im"_s, u"ℑ"_s);
        s(u"aleph"_s, u"ℵ"_s);
        s(u"beth"_s, u"ℶ"_s);
        s(u"gimel"_s, u"ℷ"_s);
        s(u"daleth"_s, u"ℸ"_s);
        s(u"wp"_s, u"℘"_s);
        s(u"mho"_s, u"℧"_s);
        s(u"angstrom"_s, u"Å"_s);
        s(u"AA"_s, u"Å"_s);
        s(u"S"_s, u"§"_s);
        s(u"P"_s, u"¶"_s);
        s(u"copyright"_s, u"©"_s);
        s(u"pounds"_s, u"£"_s);
        s(u"euro"_s, u"€"_s);
        s(u"degree"_s, u"°"_s);
        s(u"celsius"_s, u"℃"_s);
        s(u"micro"_s, u"µ"_s);
        s(u"therefore"_s, u"∴"_s);
        s(u"because"_s, u"∵"_s);
        s(u"ldots"_s, u"…"_s);
        s(u"dots"_s, u"…"_s);
        s(u"cdots"_s, u"⋯"_s);
        s(u"vdots"_s, u"⋮"_s);
        s(u"ddots"_s, u"⋱"_s);
        s(u"dotsb"_s, u"⋯"_s);
        s(u"dotsc"_s, u"…"_s);
        s(u"dotso"_s, u"…"_s);
        s(u"quad"_s, u"&#8195;"_s);
        s(u"qquad"_s, u"&#8195;&#8195;"_s);
        s(u"enspace"_s, u"&#8194;"_s);
        s(u"thinspace"_s, u"&#8201;"_s);
        return t;
    }();
    return table;
}

// --- fractions, radicals, environments ---------------------------------------------------------

QString fractionHtml(const Arg &numerator, const Arg &denominator)
{
    // Vulgar fractions for the simple cases: 1/2 -> ½.
    static const QHash<QString, QChar> vulgar{{u"1/2"_s, u'½'}, {u"1/3"_s, u'⅓'}, {u"2/3"_s, u'⅔'}, {u"1/4"_s, u'¼'},
                                              {u"3/4"_s, u'¾'}, {u"1/5"_s, u'⅕'}, {u"2/5"_s, u'⅖'}, {u"3/5"_s, u'⅗'},
                                              {u"4/5"_s, u'⅘'}, {u"1/6"_s, u'⅙'}, {u"5/6"_s, u'⅚'}, {u"1/7"_s, u'⅐'},
                                              {u"1/8"_s, u'⅛'}, {u"3/8"_s, u'⅜'}, {u"5/8"_s, u'⅝'}, {u"7/8"_s, u'⅞'},
                                              {u"1/9"_s, u'⅑'}, {u"1/10"_s, u'⅒'}};
    const auto digit = [](const QString &html) -> QString {
        return html.size() == 1 && html[0].isDigit() ? html : QString();
    };
    const QString a = digit(numerator.html);
    const QString b = digit(denominator.html);
    if (!a.isEmpty() && !b.isEmpty()) {
        const auto it = vulgar.constFind(a + u'/' + b);
        if (it != vulgar.constEnd())
            return QString(*it);
    }
    const QString top = scriptable(numerator) ? numerator.html : u'(' + numerator.html + u')';
    const QString bottom = scriptable(denominator) ? denominator.html : u'(' + denominator.html + u')';
    if (scriptable(numerator) && scriptable(denominator))
        return u"<sup>"_s + top + u"</sup>&#8260;<sub>"_s + bottom + u"</sub>"_s;
    return top + u"&#8260;"_s + bottom;
}

QString radicalHtml(const QString &index, const QString &body)
{
    QString head;
    if (index.isEmpty())
        head = u"√"_s;
    else if (index == u"3"_s)
        head = u"∛"_s;
    else if (index == u"4"_s)
        head = u"∜"_s;
    else
        head = u"√<sup>"_s + index + u"</sup>"_s;
    return head + u"<span style=\"text-decoration:overline\">"_s + body + u"</span>"_s;
}

/// `\begin{env} … \end{env}`: a matrix, `cases`, `aligned` or an `array` as plain rich text
/// (cells separated by spaces, rows by "; "). No stacking — a formula is one line here.
QString environmentHtml(QStringView body, const QString &env, bool display)
{
    QString source = body.toString();
    QString left;
    QString right;
    bool matrix = false;
    if (env == u"matrix" || env == u"smallmatrix") {
        matrix = true;
    } else if (env == u"pmatrix") {
        matrix = true;
        left = u'(';
        right = u')';
    } else if (env == u"bmatrix") {
        matrix = true;
        left = u'[';
        right = u']';
    } else if (env == u"Bmatrix") {
        matrix = true;
        left = u'{';
        right = u'}';
    } else if (env == u"vmatrix") {
        matrix = true;
        left = u'|';
        right = u'|';
    } else if (env == u"Vmatrix") {
        matrix = true;
        left = u"‖"_s;
        right = u"‖"_s;
    } else if (env == u"array" || env == u"tabular") {
        matrix = true;
        // The column specification {ccc} is not part of the cells.
        if (source.startsWith(u'{')) {
            const qsizetype close = source.indexOf(u'}');
            if (close > 0)
                source = source.mid(close + 1);
        }
    }
    const QStringList lines = source.split(u"\\\\"_s, Qt::SkipEmptyParts);
    // `aligned`, `gather`…: the alignment point (&) is where the rows line up — one space between
    // the cells reads "a = b", a grid (array, matrix) keeps its columns apart.
    const bool aligned = env == u"aligned" || env == u"align" || env == u"alignedat" || env == u"gathered"
                         || env == u"split" || env == u"multline";
    const QString separator = env == u"cases" ? u", "_s : aligned ? u" "_s : u"&nbsp;&nbsp;"_s;
    QStringList rows;
    for (const QString &line : lines) {
        const QStringList cells = line.split(u'&');
        QStringList rendered;
        for (const QString &cell : cells) {
            const QString html = assemble(parseRange(QStringView(cell.trimmed()), false, display));
            if (!html.isEmpty())
                rendered.append(html);
        }
        if (!rendered.isEmpty())
            rows.append(rendered.join(separator));
    }
    const QString joined = rows.isEmpty() ? QString()
                           : matrix       ? rows.join(u"; "_s)
                           : display      ? rows.join(u"<br/>"_s)
                                          : rows.join(u"; "_s);
    if (env == u"cases")
        return u"{ "_s + joined + u" }"_s;
    if (matrix && (!left.isEmpty() || !right.isEmpty()))
        return left + u" "_s + joined + u" "_s + right;
    return joined;
}

/// A delimiter after \left, \right, \big…: the character itself or its command ("\langle").
/// Always text the renderer escaped or built itself, never a raw character of the note: a raw
/// '<' … '>' pair around \text{…} would be a real HTML element (an image loaded from anywhere).
QString delimiterAt(QStringView s, qsizetype &i)
{
    while (i < s.size() && s[i].isSpace())
        ++i;
    if (i >= s.size())
        return {};
    if (s[i] == u'\\') {
        const qsizetype start = ++i;
        while (i < s.size() && s[i].isLetter())
            ++i;
        const QString name = s.sliced(start, i - start).toString();
        const auto it = symbols().constFind(name);
        return it != symbols().constEnd() ? it->text : QString();
    }
    const QChar c = s[i++];
    if (c == u'.')
        return {}; // \left. / \right.: no delimiter
    // \left< … \right> are angle brackets, as in LaTeX.
    if (c == u'<')
        return u"⟨"_s;
    if (c == u'>')
        return u"⟩"_s;
    return escaped(QString(c));
}

/// The next command or character as a formula, for `\not` and the argument of a command.
qsizetype parseCommand(QStringView s, qsizetype i, QList<Node> &nodes, bool display);

QList<Node> parseRange(QStringView s, bool upright, bool display)
{
    // Every nesting ({…}, arguments, scripts, environments) passes through here: deeper parts are
    // shown as their escaped source, so a hostile note cannot overflow the stack.
    static thread_local int depth = 0;
    if (depth >= MaxMathDepth)
        return {Node{escaped(s), Kind::Atom, {}, {}}};
    ++depth;
    const auto restore = qScopeGuard([] { --depth; });

    QList<Node> nodes;
    qsizetype i = 0;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == u'{') {
            const qsizetype close = matchingBrace(s, i);
            if (close < 0) {
                push(nodes, escaped(s.sliced(i + 1)), Kind::Atom);
                i = s.size();
                continue;
            }
            push(nodes, assemble(parseRange(s.sliced(i + 1, close - i - 1), upright, display)), Kind::Atom);
            i = close + 1;
            continue;
        }
        if (c == u'}') {
            ++i;
            continue;
        }
        if (c == u'^' || c == u'_') {
            ++i;
            const Arg arg = parseArg(s, i, display);
            attachScript(nodes, c, arg.html);
            continue;
        }
        if (c == u'&') { // alignment: a column break in cells (split before parsing them)
            ++i;
            continue;
        }
        if (c == u'\'') {
            if (nodes.isEmpty())
                push(nodes, u"′"_s, Kind::Atom);
            else
                nodes.last().sup += u"′"_s;
            ++i;
            continue;
        }
        if (c == u'\\') {
            i = parseCommand(s, i, nodes, display);
            continue;
        }
        if (c.isLetter()) {
            QString run;
            while (i < s.size() && s[i].isLetter())
                run += s[i++];
            push(nodes, upright ? escaped(run) : u"<i>"_s + escaped(run) + u"</i>"_s, Kind::Atom);
            continue;
        }
        if (c.isDigit()) {
            QString run;
            while (i < s.size() && (s[i].isDigit() || (s[i] == u'.' && i + 1 < s.size() && s[i + 1].isDigit())))
                run += s[i++];
            push(nodes, escaped(run), Kind::Atom);
            continue;
        }

        // Operators, punctuation and plain characters.
        const bool unary = nodes.isEmpty() || nodes.last().kind == Kind::Open || nodes.last().kind == Kind::Bin
                           || nodes.last().kind == Kind::Rel || nodes.last().kind == Kind::Punct;
        Kind kind = Kind::Atom;
        QString html;
        switch (c.unicode()) {
        case u'+':
            kind = unary ? Kind::Atom : Kind::Bin;
            html = u"+"_s;
            break;
        case u'-':
            kind = unary ? Kind::Atom : Kind::Bin;
            html = u"−"_s;
            break; // U+2212
        case u'*':
            kind = Kind::Bin;
            html = u"∗"_s;
            break;
        case u'=':
            kind = Kind::Rel;
            html = u"="_s;
            break;
        case u'<':
            kind = Kind::Rel;
            html = u"&lt;"_s;
            break;
        case u'>':
            kind = Kind::Rel;
            html = u"&gt;"_s;
            break;
        case u',': {
            // A comma between digits is a decimal comma, not punctuation ("3,14").
            const bool decimal = i > 0 && s[i - 1].isDigit() && i + 1 < s.size() && s[i + 1].isDigit();
            kind = decimal ? Kind::Atom : Kind::Punct;
            html = u","_s;
            break;
        }
        case u';':
            kind = Kind::Punct;
            html = u";"_s;
            break;
        case u':':
            kind = Kind::Rel;
            html = u":"_s;
            break;
        case u'(':
            kind = Kind::Open;
            html = u"("_s;
            break;
        case u')':
            kind = Kind::Close;
            html = u")"_s;
            break;
        case u'[':
            kind = Kind::Open;
            html = u"["_s;
            break;
        case u']':
            kind = Kind::Close;
            html = u"]"_s;
            break;
        case u'~': html = u"&nbsp;"_s; break;
        default: html = escaped(QString(c)); break;
        }
        push(nodes, html, kind);
        ++i;
    }
    return nodes;
}

qsizetype parseCommand(QStringView s, qsizetype i, QList<Node> &nodes, bool display)
{
    const auto pushN = [&nodes](const QString &html, Kind kind) { push(nodes, html, kind); };
    ++i; // the backslash
    if (i >= s.size()) {
        pushN(u"\\"_s, Kind::Atom);
        return i;
    }
    const QChar c = s[i];
    if (!c.isLetter()) {
        ++i;
        switch (c.unicode()) {
        case u',': pushN(u"&#8201;"_s, Kind::Atom); break; // thin space
        case u':': pushN(u"&#8197;"_s, Kind::Atom); break; // medium space
        case u';': pushN(u"&#8196;"_s, Kind::Atom); break; // thick space
        case u'!': break;                                  // negative thin space
        case u' ': pushN(u"&nbsp;"_s, Kind::Atom); break;
        case u'\\': pushN(u"<br/>"_s, Kind::Atom); break; // a row / line of its own
        case u'|': pushN(u"‖"_s, Kind::Atom); break;
        default: pushN(escaped(QString(c)), Kind::Atom); break;
        }
        return i;
    }
    QString name;
    while (i < s.size() && s[i].isLetter())
        name += s[i++];

    if (name == u"left" || name == u"right" || name == u"middle") {
        const QString delim = delimiterAt(s, i);
        if (!delim.isEmpty())
            pushN(delim, name == u"left" ? Kind::Open : name == u"right" ? Kind::Close : Kind::Atom);
        return i;
    }
    if (name.startsWith(u"big"_s) || name.startsWith(u"Big"_s)) {
        const QString delim = delimiterAt(s, i);
        if (!delim.isEmpty())
            pushN(delim, name.endsWith(u"l"_s) ? Kind::Open : name.endsWith(u"r"_s) ? Kind::Close : Kind::Atom);
        return i;
    }

    if (name == u"frac" || name == u"dfrac" || name == u"tfrac" || name == u"cfrac" || name == u"sfrac") {
        if (i < s.size() && s[i] == u'[') { // \cfrac[l]{…}{…}
            const qsizetype close = s.indexOf(u']', i);
            if (close > i)
                i = close + 1;
        }
        const Arg numerator = parseArg(s, i, display);
        const Arg denominator = parseArg(s, i, display);
        pushN(fractionHtml(numerator, denominator), Kind::Atom);
        return i;
    }
    if (name == u"binom" || name == u"dbinom" || name == u"tbinom") {
        const Arg top = parseArg(s, i, display);
        const Arg bottom = parseArg(s, i, display);
        pushN(u"("_s + fractionHtml(top, bottom) + u")"_s, Kind::Atom);
        return i;
    }
    if (name == u"sqrt") {
        QString index;
        if (i < s.size() && s[i] == u'[') {
            const qsizetype close = s.indexOf(u']', i);
            if (close > i) {
                index = assemble(parseRange(s.sliced(i + 1, close - i - 1), false, display));
                i = close + 1;
            }
        }
        const Arg arg = parseArg(s, i, display);
        pushN(radicalHtml(index, arg.html), Kind::Atom);
        return i;
    }

    // Text and math alphabets.
    const bool rawText = name == u"text" || name == u"mbox" || name == u"textrm" || name == u"textnormal"
                         || name == u"textup" || name == u"textsf" || name == u"texttt" || name == u"textsc"
                         || name == u"textbf" || name == u"textit" || name == u"operatorname";
    const bool mathAlphabet = name == u"mathrm" || name == u"mathnormal" || name == u"mathbf" || name == u"boldsymbol"
                              || name == u"bm" || name == u"mathit" || name == u"mathsf" || name == u"mathtt"
                              || name == u"mathbb" || name == u"mathcal" || name == u"mathfrak";
    if (rawText || mathAlphabet) {
        if (i < s.size() && s[i] == u'{') {
            const qsizetype close = matchingBrace(s, i);
            if (close < 0) {
                pushN(escaped(s.sliced(i + 1)), Kind::Atom);
                i = s.size();
                return i;
            }
            const QStringView inner = s.sliced(i + 1, close - i - 1);
            i = close + 1;
            if (rawText) {
                // The text keeps its own spaces ("\text{jeśli } x" separates the words); a line
                // break inside becomes a space.
                QString html = escaped(inner).replace(u'\n', u' ');
                if (name == u"textbf")
                    html = u"<b>"_s + html + u"</b>"_s;
                else if (name == u"textit")
                    html = u"<i>"_s + html + u"</i>"_s;
                else if (name == u"texttt")
                    html = u"<span style=\"font-family:monospace\">"_s + html + u"</span>"_s;
                else if (name == u"textsc")
                    html = u"<span style=\"font-variant:small-caps\">"_s + html + u"</span>"_s;
                pushN(html, name == u"operatorname" ? Kind::Func : Kind::Atom);
                return i;
            }
            bool upright = name == u"mathrm" || name == u"mathbf" || name == u"mathsf" || name == u"mathtt"
                           || name == u"mathbb";
            QString html = assemble(parseRange(inner, upright, display));
            if (name == u"mathbb" && html.size() == 1) {
                // Letters with a Unicode double-struck form: ℝ, ℕ, ℤ, ℚ, ℂ, ℍ, ℙ.
                static const QHash<QChar, QChar> doubleStruck{{u'C', u'ℂ'}, {u'H', u'ℍ'}, {u'N', u'ℕ'}, {u'P', u'ℙ'},
                                                              {u'Q', u'ℚ'}, {u'R', u'ℝ'}, {u'Z', u'ℤ'}};
                const auto it = doubleStruck.constFind(html[0]);
                if (it != doubleStruck.constEnd())
                    html = QString(*it);
            }
            if (name == u"mathbf" || name == u"boldsymbol" || name == u"bm")
                html = u"<b>"_s + html + u"</b>"_s;
            else if (name == u"mathsf")
                html = u"<span style=\"font-family:sans-serif\">"_s + html + u"</span>"_s;
            else if (name == u"mathtt")
                html = u"<span style=\"font-family:monospace\">"_s + html + u"</span>"_s;
            pushN(html, Kind::Atom);
            return i;
        }
        const Arg arg = parseArg(s, i, display);
        pushN(arg.html, name == u"operatorname" ? Kind::Func : Kind::Atom);
        return i;
    }

    // Decorations over or under the argument.
    if (name == u"overline" || name == u"underline" || name == u"widehat" || name == u"widetilde"
        || name == u"overrightarrow" || name == u"overleftarrow" || name == u"overbrace" || name == u"underbrace") {
        const Arg arg = parseArg(s, i, display);
        QString html = arg.html;
        if (name != u"overbrace" && name != u"underbrace")
            html = u"<span style=\"text-decoration:"_s + (name == u"underline" ? u"underline"_s : u"overline"_s)
                   + u"\">"_s + html + u"</span>"_s;
        pushN(html, Kind::Atom);
        return i;
    }

    // Accents: a combining mark on the last character of the argument.
    static const QHash<QString, QChar> accents{
            {u"hat"_s, QChar(0x0302)},     {u"widehat"_s, QChar(0x0302)},   {u"bar"_s, QChar(0x0304)},
            {u"tilde"_s, QChar(0x0303)},   {u"widetilde"_s, QChar(0x0303)}, {u"vec"_s, QChar(0x20D7)},
            {u"dot"_s, QChar(0x0307)},     {u"ddot"_s, QChar(0x0308)},      {u"acute"_s, QChar(0x0301)},
            {u"grave"_s, QChar(0x0300)},   {u"check"_s, QChar(0x030C)},     {u"breve"_s, QChar(0x0306)},
            {u"mathring"_s, QChar(0x030A)}};
    if (const auto it = accents.constFind(name); it != accents.constEnd()) {
        const Arg arg = parseArg(s, i, display);
        pushN(withCombining(arg.html, *it), Kind::Atom);
        return i;
    }

    if (name == u"not") {
        const Arg arg = parseArg(s, i, display);
        static const QHash<QString, QString> negated{
                {u"="_s, u"≠"_s}, {u"&lt;"_s, u"≮"_s}, {u"&gt;"_s, u"≯"_s}, {u"∈"_s, u"∉"_s}, {u"∋"_s, u"∌"_s},
                {u"≤"_s, u"≰"_s}, {u"≥"_s, u"≱"_s},    {u"≈"_s, u"≉"_s},    {u"≡"_s, u"≢"_s}, {u"⊂"_s, u"⊄"_s},
                {u"⊃"_s, u"⊅"_s}, {u"⊆"_s, u"⊈"_s},    {u"⊇"_s, u"⊉"_s},    {u"∣"_s, u"∤"_s}, {u"∥"_s, u"∦"_s}};
        const auto it = negated.constFind(arg.html);
        pushN(it != negated.constEnd() ? it.value() : arg.html + QChar(0x0338), Kind::Rel);
        return i;
    }
    if (name == u"pmod") {
        const Arg arg = parseArg(s, i, display);
        pushN(u"&nbsp;(mod "_s + arg.html + u')', Kind::Atom);
        return i;
    }
    if (name == u"overset" || name == u"underset" || name == u"stackrel") {
        const Arg over = parseArg(s, i, display);
        const Arg base = parseArg(s, i, display);
        Node node{base.html, Kind::Atom, {}, {}};
        if (name == u"underset")
            node.sub = over.html;
        else
            node.sup = over.html;
        nodes.append(node);
        return i;
    }
    if (name == u"genfrac") {
        parseArg(s, i, display); // left delimiter (ignored: the fraction keeps its own shape)
        parseArg(s, i, display); // right delimiter
        parseArg(s, i, display); // thickness
        parseArg(s, i, display); // style
        const Arg numerator = parseArg(s, i, display);
        const Arg denominator = parseArg(s, i, display);
        pushN(fractionHtml(numerator, denominator), Kind::Atom);
        return i;
    }
    if (name == u"substack") {
        const Arg arg = parseArg(s, i, display);
        pushN(arg.html, Kind::Atom);
        return i;
    }
    if (name == u"hspace" || name == u"phantom" || name == u"hphantom" || name == u"vphantom") {
        const Arg arg = parseArg(s, i, display);
        if (name != u"vphantom")
            pushN(u"&#8195;"_s, Kind::Atom);
        return i;
    }
    if (name == u"color") {
        parseArg(s, i, display); // the rest of the group keeps the surrounding colour
        return i;
    }
    if (name == u"textcolor") {
        parseArg(s, i, display);
        const Arg arg = parseArg(s, i, display);
        pushN(arg.html, Kind::Atom);
        return i;
    }
    if (name == u"tag" || name == u"label") {
        if (i < s.size() && s[i] == u'{')
            parseArg(s, i, display);
        return i;
    }
    if (name == u"mathop" || name == u"mathrel" || name == u"mathbin" || name == u"mathord" || name == u"mathopen"
        || name == u"mathclose" || name == u"mathpunct") {
        const Arg arg = parseArg(s, i, display);
        const Kind kind = name == u"mathrel"     ? Kind::Rel
                          : name == u"mathbin"   ? Kind::Bin
                          : name == u"mathopen"  ? Kind::Open
                          : name == u"mathclose" ? Kind::Close
                          : name == u"mathpunct" ? Kind::Punct
                                                 : Kind::Atom;
        pushN(arg.html, kind);
        return i;
    }
    if (name == u"begin") {
        if (i < s.size() && s[i] == u'{') {
            const qsizetype close = matchingBrace(s, i);
            if (close > i) {
                const QString env = s.sliced(i + 1, close - i - 1).toString();
                i = close + 1;
                const QString marker = u"\\end{"_s + env + u'}';
                const qsizetype end = s.indexOf(marker, i);
                const QStringView body = end < 0 ? s.sliced(i) : s.sliced(i, end - i);
                i = end < 0 ? s.size() : end + marker.size();
                pushN(environmentHtml(body, env, display), Kind::Atom);
                return i;
            }
        }
        return i;
    }

    const auto it = symbols().constFind(name);
    if (it != symbols().constEnd()) {
        pushN(it->text, it->kind);
        return i;
    }
    // Not understood: shown as written, so a note never loses what it says.
    pushN(escaped(QString(u'\\' + name)), Kind::Atom);
    return i;
}

} // namespace

QList<MathSpan> findMathSpans(QStringView text)
{
    QList<MathSpan> spans;
    if (text.indexOf(u'$') < 0 && text.indexOf(u'\\') < 0)
        return spans;
    for (qsizetype i = 0; i < text.size();) {
        const QChar c = text[i];
        if (c == u'\\' && i + 1 < text.size()) {
            const QChar next = text[i + 1];
            if (next == u'(' || next == u'[') {
                const QChar close = next == u'(' ? u')' : u']';
                qsizetype end = -1;
                for (qsizetype j = i + 2; j + 1 < text.size();) {
                    if (text[j] == u'\\' && isAsciiPunct(text[j + 1])) {
                        if (text[j + 1] == close) {
                            end = j;
                            break;
                        }
                        j += 2;
                        continue;
                    }
                    ++j;
                }
                if (end < 0)
                    break; // no closing \(… anywhere after this one either
                if (end > i + 2 && !text.sliced(i + 2, end - i - 2).trimmed().isEmpty()) {
                    spans.append(MathSpan{i, end + 2, i + 2, end, next == u'['});
                    i = end + 2;
                    continue;
                }
                i += 2;
                continue;
            }
            i += isAsciiPunct(next) ? 2 : 1;
            continue;
        }
        if (c != u'$') {
            ++i;
            continue;
        }
        const bool display = i + 1 < text.size() && text[i + 1] == u'$';
        const qsizetype open = i + (display ? 2 : 1);
        // `$ x $` is not a formula (a space follows the opening `$`); `$$…$$` may span lines.
        if (!display && (open >= text.size() || text[open].isSpace())) {
            i = open;
            continue;
        }
        qsizetype end = -1;
        for (qsizetype j = open; j < text.size();) {
            if (text[j] == u'\\' && j + 1 < text.size() && isAsciiPunct(text[j + 1])) {
                j += 2;
                continue;
            }
            if (text[j] == u'$') {
                if (display) {
                    if (j + 1 < text.size() && text[j + 1] == u'$') {
                        end = j;
                        break;
                    }
                } else if (j > open && !text[j - 1].isSpace() && (j + 1 >= text.size() || !text[j + 1].isDigit())) {
                    end = j;
                    break;
                }
            }
            ++j;
        }
        if (end < 0)
            break; // an unclosed delimiter: no `$…$` later either (the closing rules are local)
        const qsizetype closeLen = display ? 2 : 1;
        if (text.sliced(open, end - open).trimmed().isEmpty()) {
            i = end + closeLen;
            continue;
        }
        spans.append(MathSpan{i, end + closeLen, open, end, display});
        i = end + closeLen;
    }
    return spans;
}

QString renderMath(QStringView latex, bool display)
{
    const QString out = assemble(parseRange(latex, false, display));
    if (out.isEmpty())
        return out;
    // A display formula keeps its own lines; an inline one joins them with a space.
    return display ? out : QString(out).replace(u"<br/>"_s, u" "_s);
}

} // namespace brulion::markdown
