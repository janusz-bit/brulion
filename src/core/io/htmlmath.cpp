#include "io/htmlmath_p.h"

#include "io/htmlutil_p.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// Formulas taken from one page; later ones stay as the page has them.
constexpr int MaxFormulas = 10000;
/// Tags read: a page with more is not read any further (memory).
constexpr qsizetype MaxTags = 1'500'000;
/// MathML nesting and number of elements converted; beyond them the text content is taken.
constexpr int MaxMathDepth = 40;
constexpr int MaxMathNodes = 20000;

// --- tags --------------------------------------------------------------------------------------

struct Tag {
    qsizetype start = 0; ///< [start, end) of the whole tag
    qsizetype end = 0;
    qsizetype attrFrom = 0; ///< the attribute text
    qsizetype attrTo = 0;
    QString name; ///< lower case
    bool closing = false;
    bool leaf = false; ///< void or self-closing
    int match = -1;    ///< a start tag: its end tag; an end tag: its start tag; -1 when none
};

bool isTagNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'-' || c == u':' || c == u'_';
}

/// Index of the '>' ending the tag whose attributes begin at `from` (quotes may hold '>'), -1 if none.
qsizetype startTagEnd(const QString &html, qsizetype from)
{
    QChar quote;
    for (qsizetype i = from; i < html.size(); ++i) {
        const QChar c = html[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'>') {
            return i;
        }
    }
    return quote.isNull() ? -1 : html.indexOf(u'>', from);
}

/// The tags of `html` in order, start and end tags matched: an end tag closes the nearest open
/// element of its name (those above it are closed implicitly, their `match` stays -1); one with
/// no open element is a stray. Comments are skipped, the content of raw-text elements (`script`,
/// `style`, `textarea`, `title`, …) is no markup. Linear time.
QList<Tag> tokenize(const QString &html)
{
    static const QSet<QString> voids = {u"area"_s,  u"base"_s,   u"br"_s,    u"col"_s,  u"embed"_s,
                                        u"hr"_s,    u"img"_s,    u"input"_s, u"link"_s, u"meta"_s,
                                        u"param"_s, u"source"_s, u"track"_s, u"wbr"_s};
    static const QSet<QString> rawText = {u"script"_s, u"style"_s,  u"textarea"_s, u"title"_s,
                                          u"xmp"_s,    u"iframe"_s, u"noembed"_s,  u"noframes"_s};
    QList<Tag> tags;
    QList<int> stack;
    QHash<QString, int> open;
    const qsizetype size = html.size();
    qsizetype i = html.indexOf(u'<');
    while (i >= 0 && i + 1 < size && tags.size() < MaxTags) {
        if (QStringView(html).sliced(i).startsWith(u"<!--")) {
            const qsizetype e = html.indexOf(u"-->"_s, i + 4);
            if (e < 0)
                break;
            i = html.indexOf(u'<', e + 3);
            continue;
        }
        const QChar c = html[i + 1];
        if (c == u'!' || c == u'?') {
            const qsizetype gt = html.indexOf(u'>', i + 2);
            if (gt < 0)
                break;
            i = html.indexOf(u'<', gt + 1);
            continue;
        }
        const bool closing = c == u'/';
        const qsizetype nameStart = i + 1 + (closing ? 1 : 0);
        qsizetype nameEnd = nameStart;
        while (nameEnd < size && isTagNameChar(html[nameEnd]))
            ++nameEnd;
        if (nameEnd == nameStart || !html[nameStart].isLetter()) {
            i = html.indexOf(u'<', i + 1);
            continue;
        }
        const qsizetype gt = closing ? html.indexOf(u'>', nameEnd) : startTagEnd(html, nameEnd);
        if (gt < 0)
            break;
        Tag t;
        t.start = i;
        t.end = gt + 1;
        t.attrFrom = nameEnd;
        t.attrTo = gt;
        t.name = html.sliced(nameStart, nameEnd - nameStart).toLower();
        t.closing = closing;
        const int index = int(tags.size());
        if (closing) {
            if (open.value(t.name) > 0) {
                while (!stack.isEmpty()) {
                    const int s = stack.takeLast();
                    --open[tags[s].name];
                    if (tags[s].name == t.name) {
                        tags[s].match = index;
                        t.match = s;
                        break;
                    }
                }
            }
            tags.append(t);
        } else {
            if (html[gt - 1] == u'/') {
                t.leaf = true;
                t.attrTo = gt - 1;
            } else {
                t.leaf = voids.contains(t.name);
            }
            tags.append(t);
            if (!t.leaf) {
                stack.append(index);
                ++open[t.name];
                if (rawText.contains(t.name)) {
                    const QString close = u"</"_s + t.name;
                    qsizetype at = html.indexOf(close, gt + 1, Qt::CaseInsensitive);
                    while (at >= 0 && at + close.size() < size && isTagNameChar(html[at + close.size()]))
                        at = html.indexOf(close, at + close.size(), Qt::CaseInsensitive);
                    if (at < 0)
                        break;
                    i = at;
                    continue;
                }
            }
        }
        i = html.indexOf(u'<', gt + 1);
    }
    return tags;
}

/// `name` without a namespace prefix (`m:math`).
QString localName(const QString &name)
{
    const qsizetype colon = name.lastIndexOf(u':');
    return colon < 0 ? name : name.sliced(colon + 1);
}

// --- MathML text -------------------------------------------------------------------------------

/// Text of MathML (and of a formula's source): entities decoded, the named ones MathML pages use
/// too, the invisible operators (function application, invisible times…) and zero-width spaces
/// dropped.
QString mathText(const QString &raw)
{
    QString s = decodeHtmlEntities(raw);
    if (s.contains(u'&')) {
        static const QHash<QString, QString> named = {{u"InvisibleTimes"_s, QString()},
                                                      {u"ApplyFunction"_s, QString()},
                                                      {u"InvisibleComma"_s, QString()},
                                                      {u"InvisiblePlus"_s, QString()},
                                                      {u"it"_s, QString()},
                                                      {u"af"_s, QString()},
                                                      {u"ic"_s, QString()},
                                                      {u"minus"_s, u"−"_s},
                                                      {u"times"_s, u"×"_s},
                                                      {u"middot"_s, u"·"_s},
                                                      {u"sdot"_s, u"⋅"_s},
                                                      {u"plusmn"_s, u"±"_s},
                                                      {u"divide"_s, u"÷"_s},
                                                      {u"deg"_s, u"°"_s},
                                                      {u"prime"_s, u"′"_s},
                                                      {u"hellip"_s, u"…"_s},
                                                      {u"ne"_s, u"≠"_s},
                                                      {u"le"_s, u"≤"_s},
                                                      {u"ge"_s, u"≥"_s},
                                                      {u"infin"_s, u"∞"_s},
                                                      {u"part"_s, u"∂"_s},
                                                      {u"PartialD"_s, u"∂"_s},
                                                      {u"nabla"_s, u"∇"_s},
                                                      {u"sum"_s, u"∑"_s},
                                                      {u"Sum"_s, u"∑"_s},
                                                      {u"prod"_s, u"∏"_s},
                                                      {u"int"_s, u"∫"_s},
                                                      {u"Integral"_s, u"∫"_s},
                                                      {u"radic"_s, u"√"_s},
                                                      {u"rarr"_s, u"→"_s},
                                                      {u"larr"_s, u"←"_s},
                                                      {u"rArr"_s, u"⇒"_s},
                                                      {u"lArr"_s, u"⇐"_s},
                                                      {u"hArr"_s, u"⇔"_s},
                                                      {u"isin"_s, u"∈"_s},
                                                      {u"notin"_s, u"∉"_s},
                                                      {u"cup"_s, u"∪"_s},
                                                      {u"cap"_s, u"∩"_s},
                                                      {u"alpha"_s, u"α"_s},
                                                      {u"beta"_s, u"β"_s},
                                                      {u"gamma"_s, u"γ"_s},
                                                      {u"delta"_s, u"δ"_s},
                                                      {u"epsilon"_s, u"ϵ"_s},
                                                      {u"theta"_s, u"θ"_s},
                                                      {u"lambda"_s, u"λ"_s},
                                                      {u"mu"_s, u"μ"_s},
                                                      {u"pi"_s, u"π"_s},
                                                      {u"sigma"_s, u"σ"_s},
                                                      {u"phi"_s, u"ϕ"_s},
                                                      {u"omega"_s, u"ω"_s},
                                                      {u"Delta"_s, u"Δ"_s},
                                                      {u"Sigma"_s, u"Σ"_s},
                                                      {u"Omega"_s, u"Ω"_s}};
        static const QRegularExpression entity(u"&([A-Za-z][A-Za-z0-9]*);"_s);
        QString out;
        qsizetype last = 0;
        for (auto it = entity.globalMatch(s); it.hasNext();) {
            const auto m = it.next();
            const auto found = named.constFind(m.captured(1));
            if (found == named.constEnd())
                continue;
            out += QStringView(s).sliced(last, m.capturedStart() - last);
            out += *found;
            last = m.capturedEnd();
        }
        if (last > 0) {
            out += QStringView(s).sliced(last);
            s = out;
        }
    }
    s.removeIf([](QChar c) {
        const char16_t u = c.unicode();
        return (u >= 0x2061 && u <= 0x2064) || u == 0x200B || u == 0xFEFF;
    });
    return s;
}

/// Whitespace runs (also NBSP and line breaks) as one space, trimmed.
QString simplified(const QString &s)
{
    QString out;
    out.reserve(s.size());
    bool space = false;
    for (const QChar c : s) {
        if (c.isSpace() || c == QChar::Nbsp) {
            space = !out.isEmpty();
            continue;
        }
        if (space)
            out += u' ';
        space = false;
        out += c;
    }
    return out;
}

/// Whitespace runs (also NBSP and line breaks) as one space each, nothing trimmed.
QString collapsedSpaces(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const bool space = c.isSpace() || c == QChar::Nbsp;
        if (!space || out.isEmpty() || out.back() != u' ')
            out += space ? QChar(u' ') : c;
    }
    return out;
}

/// The number a CSS-like length starts with ("0", "1.5em", "-2px"), nullopt when it has none.
std::optional<double> leadingNumber(const QString &s)
{
    const QString t = s.trimmed();
    qsizetype n = 0;
    while (n < t.size() && (t[n].isDigit() || t[n] == u'.' || (n == 0 && (t[n] == u'-' || t[n] == u'+'))))
        ++n;
    bool ok = false;
    const double value = t.left(n).toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

// --- MathML -> TeX -----------------------------------------------------------------------------

struct Node {
    QString name; ///< local name, lower case
    QString attrs;
    QString text; ///< the text directly inside (entities decoded)
    QList<Node> kids;

    std::optional<QString> attr(const QString &key) const { return htmlAttribute(attrs, key); }
};

struct SymbolRow {
    const char16_t *from;
    const char16_t *to;
};

const QHash<QString, QString> &symbols()
{
    static const SymbolRow rows[] = {{u"α", u"\\alpha"},
                                     {u"β", u"\\beta"},
                                     {u"γ", u"\\gamma"},
                                     {u"δ", u"\\delta"},
                                     {u"ε", u"\\varepsilon"},
                                     {u"ϵ", u"\\epsilon"},
                                     {u"ζ", u"\\zeta"},
                                     {u"η", u"\\eta"},
                                     {u"θ", u"\\theta"},
                                     {u"ϑ", u"\\vartheta"},
                                     {u"ι", u"\\iota"},
                                     {u"κ", u"\\kappa"},
                                     {u"λ", u"\\lambda"},
                                     {u"μ", u"\\mu"},
                                     {u"ν", u"\\nu"},
                                     {u"ξ", u"\\xi"},
                                     {u"π", u"\\pi"},
                                     {u"ϖ", u"\\varpi"},
                                     {u"ρ", u"\\rho"},
                                     {u"ϱ", u"\\varrho"},
                                     {u"σ", u"\\sigma"},
                                     {u"ς", u"\\varsigma"},
                                     {u"τ", u"\\tau"},
                                     {u"υ", u"\\upsilon"},
                                     {u"φ", u"\\varphi"},
                                     {u"ϕ", u"\\phi"},
                                     {u"χ", u"\\chi"},
                                     {u"ψ", u"\\psi"},
                                     {u"ω", u"\\omega"},
                                     {u"Γ", u"\\Gamma"},
                                     {u"Δ", u"\\Delta"},
                                     {u"Θ", u"\\Theta"},
                                     {u"Λ", u"\\Lambda"},
                                     {u"Ξ", u"\\Xi"},
                                     {u"Π", u"\\Pi"},
                                     {u"Σ", u"\\Sigma"},
                                     {u"Υ", u"\\Upsilon"},
                                     {u"Φ", u"\\Phi"},
                                     {u"Ψ", u"\\Psi"},
                                     {u"Ω", u"\\Omega"},
                                     {u"∑", u"\\sum"},
                                     {u"∏", u"\\prod"},
                                     {u"∐", u"\\coprod"},
                                     {u"∫", u"\\int"},
                                     {u"∬", u"\\iint"},
                                     {u"∭", u"\\iiint"},
                                     {u"∮", u"\\oint"},
                                     {u"⋃", u"\\bigcup"},
                                     {u"⋂", u"\\bigcap"},
                                     {u"⋁", u"\\bigvee"},
                                     {u"⋀", u"\\bigwedge"},
                                     {u"⨁", u"\\bigoplus"},
                                     {u"⨂", u"\\bigotimes"},
                                     {u"⨀", u"\\bigodot"},
                                     {u"∂", u"\\partial"},
                                     {u"∇", u"\\nabla"},
                                     {u"∞", u"\\infty"},
                                     {u"±", u"\\pm"},
                                     {u"∓", u"\\mp"},
                                     {u"×", u"\\times"},
                                     {u"÷", u"\\div"},
                                     {u"⋅", u"\\cdot"},
                                     {u"·", u"\\cdot"},
                                     {u"∘", u"\\circ"},
                                     {u"∙", u"\\bullet"},
                                     {u"•", u"\\bullet"},
                                     {u"∗", u"\\ast"},
                                     {u"⋆", u"\\star"},
                                     {u"⊕", u"\\oplus"},
                                     {u"⊗", u"\\otimes"},
                                     {u"⊙", u"\\odot"},
                                     {u"⊖", u"\\ominus"},
                                     {u"≤", u"\\le"},
                                     {u"⩽", u"\\leqslant"},
                                     {u"≥", u"\\ge"},
                                     {u"⩾", u"\\geqslant"},
                                     {u"≠", u"\\ne"},
                                     {u"≈", u"\\approx"},
                                     {u"≡", u"\\equiv"},
                                     {u"≅", u"\\cong"},
                                     {u"∼", u"\\sim"},
                                     {u"≃", u"\\simeq"},
                                     {u"∝", u"\\propto"},
                                     {u"≪", u"\\ll"},
                                     {u"≫", u"\\gg"},
                                     {u"≺", u"\\prec"},
                                     {u"≻", u"\\succ"},
                                     {u"⊥", u"\\perp"},
                                     {u"∥", u"\\parallel"},
                                     {u"‖", u"\\|"},
                                     {u"∣", u"\\mid"},
                                     {u"→", u"\\to"},
                                     {u"←", u"\\leftarrow"},
                                     {u"↔", u"\\leftrightarrow"},
                                     {u"⇒", u"\\Rightarrow"},
                                     {u"⇐", u"\\Leftarrow"},
                                     {u"⇔", u"\\Leftrightarrow"},
                                     {u"↦", u"\\mapsto"},
                                     {u"↑", u"\\uparrow"},
                                     {u"↓", u"\\downarrow"},
                                     {u"⟶", u"\\longrightarrow"},
                                     {u"⟹", u"\\Longrightarrow"},
                                     {u"⟵", u"\\longleftarrow"},
                                     {u"⟺", u"\\Longleftrightarrow"},
                                     {u"⟼", u"\\longmapsto"},
                                     {u"↗", u"\\nearrow"},
                                     {u"↘", u"\\searrow"},
                                     {u"⇌", u"\\rightleftharpoons"},
                                     {u"∈", u"\\in"},
                                     {u"∉", u"\\notin"},
                                     {u"∋", u"\\ni"},
                                     {u"⊂", u"\\subset"},
                                     {u"⊃", u"\\supset"},
                                     {u"⊆", u"\\subseteq"},
                                     {u"⊇", u"\\supseteq"},
                                     {u"⊄", u"\\not\\subset"},
                                     {u"∪", u"\\cup"},
                                     {u"∩", u"\\cap"},
                                     {u"∖", u"\\setminus"},
                                     {u"∅", u"\\emptyset"},
                                     {u"⊎", u"\\uplus"},
                                     {u"⊔", u"\\sqcup"},
                                     {u"⊓", u"\\sqcap"},
                                     {u"∀", u"\\forall"},
                                     {u"∃", u"\\exists"},
                                     {u"∄", u"\\nexists"},
                                     {u"¬", u"\\neg"},
                                     {u"∧", u"\\wedge"},
                                     {u"∨", u"\\vee"},
                                     {u"⊢", u"\\vdash"},
                                     {u"⊨", u"\\models"},
                                     {u"⊤", u"\\top"},
                                     {u"…", u"\\ldots"},
                                     {u"⋯", u"\\cdots"},
                                     {u"⋮", u"\\vdots"},
                                     {u"⋱", u"\\ddots"},
                                     {u"′", u"'"},
                                     {u"″", u"''"},
                                     {u"‴", u"'''"},
                                     {u"°", u"^\\circ"},
                                     {u"ℝ", u"\\mathbb{R}"},
                                     {u"ℕ", u"\\mathbb{N}"},
                                     {u"ℤ", u"\\mathbb{Z}"},
                                     {u"ℚ", u"\\mathbb{Q}"},
                                     {u"ℂ", u"\\mathbb{C}"},
                                     {u"ℙ", u"\\mathbb{P}"},
                                     {u"ℍ", u"\\mathbb{H}"},
                                     {u"ℓ", u"\\ell"},
                                     {u"ℏ", u"\\hbar"},
                                     {u"ħ", u"\\hbar"},
                                     {u"ℜ", u"\\Re"},
                                     {u"ℑ", u"\\Im"},
                                     {u"ℵ", u"\\aleph"},
                                     {u"℘", u"\\wp"},
                                     {u"⟨", u"\\langle"},
                                     {u"⟩", u"\\rangle"},
                                     {u"〈", u"\\langle"},
                                     {u"〉", u"\\rangle"},
                                     {u"⌈", u"\\lceil"},
                                     {u"⌉", u"\\rceil"},
                                     {u"⌊", u"\\lfloor"},
                                     {u"⌋", u"\\rfloor"},
                                     {u"−", u"-"},
                                     {u"–", u"-"},
                                     {u"‐", u"-"},
                                     {u"∠", u"\\angle"},
                                     {u"△", u"\\triangle"},
                                     {u"□", u"\\square"},
                                     {u"∎", u"\\blacksquare"},
                                     {u"∴", u"\\therefore"},
                                     {u"∵", u"\\because"},
                                     {u"{", u"\\{"},
                                     {u"}", u"\\}"},
                                     {u"&", u"\\&"},
                                     {u"#", u"\\#"},
                                     {u"%", u"\\%"},
                                     {u"$", u"\\$"},
                                     {u"_", u"\\_"},
                                     {u"\\", u"\\backslash"},
                                     {u"~", u"\\sim"},
                                     {u"^", u"\\wedge"},
                                     {u"ˆ", u"\\hat{}"}};
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> h;
        for (const SymbolRow &r : rows)
            h.insert(QString::fromUtf16(r.from), QString::fromUtf16(r.to));
        return h;
    }();
    return table;
}

/// A letter of a Unicode mathematical alphabet as TeX (`𝑥` → `x`, `𝐱` → `\mathbf{x}`, `𝔸` →
/// `\mathbb{A}`), nullopt for any other code point.
std::optional<QString> alphanumericTex(uint cp)
{
    const auto letter = [](uint index) { return QString(QChar(index < 26 ? u'A' + index : u'a' + (index - 26))); };
    if (cp == 0x210E)
        return u"h"_s;
    if (cp >= 0x1D434 && cp <= 0x1D467) // italic
        return letter(cp - 0x1D434);
    if (cp >= 0x1D400 && cp <= 0x1D433) // bold
        return u"\\mathbf{"_s + letter(cp - 0x1D400) + u'}';
    if (cp >= 0x1D468 && cp <= 0x1D49B) // bold italic
        return u"\\boldsymbol{"_s + letter(cp - 0x1D468) + u'}';
    if (cp >= 0x1D538 && cp <= 0x1D56B) // double-struck
        return u"\\mathbb{"_s + letter(cp - 0x1D538) + u'}';
    if (cp >= 0x1D49C && cp <= 0x1D4CF) // script
        return u"\\mathcal{"_s + letter(cp - 0x1D49C) + u'}';
    if (cp >= 0x1D504 && cp <= 0x1D537) // fraktur
        return u"\\mathfrak{"_s + letter(cp - 0x1D504) + u'}';
    if (cp >= 0x1D7D8 && cp <= 0x1D7E1) // double-struck digits
        return u"\\mathbb{"_s + QString::number(cp - 0x1D7D8) + u'}';
    if (cp >= 0x1D7CE && cp <= 0x1D7D7) // bold digits
        return u"\\mathbf{"_s + QString::number(cp - 0x1D7CE) + u'}';
    return std::nullopt;
}

/// `a` followed by `b`; a space between them when `a` ends with a control word and `b` starts
/// with a letter (`\alpha x`).
QString joinTex(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    qsizetype k = a.size();
    while (k > 0 && a[k - 1].unicode() < 128 && a[k - 1].isLetter())
        --k;
    if (k < a.size() && k > 0 && a[k - 1] == u'\\' && b[0].isLetter())
        return a + u' ' + b;
    return a + b;
}

/// `text` (of an `<mo>` or `<mi>`) as TeX, character by character.
QString symbolTex(const QString &text)
{
    if (const auto whole = symbols().constFind(text); whole != symbols().constEnd())
        return *whole;
    QString out;
    for (const uint cp : text.toUcs4()) {
        const char32_t c32 = cp;
        const QString one = QString::fromUcs4(&c32, 1);
        QString piece;
        if (const auto alnum = alphanumericTex(cp))
            piece = *alnum;
        else if (const auto found = symbols().constFind(one); found != symbols().constEnd())
            piece = *found;
        else
            piece = one;
        out = out.isEmpty() ? piece : joinTex(out, piece);
    }
    return out;
}

QString texOf(const Node &n);
QString rowOf(const QList<Node> &kids);

/// `\text{…}` of plain text: what TeX would read as syntax is escaped.
QString textCommand(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'\\': out += u"\\textbackslash{}"_s; break;
        case u'{': out += u"\\{"_s; break;
        case u'}': out += u"\\}"_s; break;
        case u'#': out += u"\\#"_s; break;
        case u'%': out += u"\\%"_s; break;
        case u'&': out += u"\\&"_s; break;
        case u'_': out += u"\\_"_s; break;
        case u'$': out += u"\\$"_s; break;
        case u'^': out += u"\\^{}"_s; break;
        case u'~': out += u"\\~{}"_s; break;
        case 0xA0: out += u' '; break;
        default: out += c;
        }
    }
    return u"\\text{"_s + out + u'}';
}

QString group(const QString &s)
{
    return u'{' + s + u'}';
}

/// An exponent or index: a single letter or digit as it is, anything else in braces.
QString scriptArg(const QString &s, bool alwaysBraced)
{
    if (!alwaysBraced && s.size() == 1 && s[0].unicode() < 128 && s[0].isLetterOrNumber())
        return s;
    return group(s);
}

/// Is `tex` one TeX atom — a character, a control word or a control word with one group
/// (`\alpha`, `\mathbb{R}`) — so that a script may follow it without braces around it?
bool isAtomicTex(const QString &tex)
{
    if (tex.isEmpty())
        return false;
    if (tex.size() == 1)
        return tex[0] != u'}' && tex[0] != u'\'';
    static const QRegularExpression atom(u"^\\\\[A-Za-z]+(?:\\{[^{}]*\\})?$"_s);
    return atom.match(tex).hasMatch();
}

bool isToken(const Node &n)
{
    return n.name == u"mi" || n.name == u"mn" || n.name == u"mo" || n.name == u"mtext" || n.name == u"ms";
}

QString tokenText(const Node &n)
{
    return simplified(n.text);
}

bool isOpeningFence(const QString &t)
{
    return t == u"(" || t == u"[" || t == u"{" || t == u"|" || t == u"‖" || t == u"∥" || t == u"⟨" || t == u"⌊"
           || t == u"⌈";
}

bool isClosingFence(const QString &t)
{
    return t == u")" || t == u"]" || t == u"}" || t == u"|" || t == u"‖" || t == u"∥" || t == u"⟩" || t == u"⌋"
           || t == u"⌉";
}

/// The only element a row-like element holds (`<mrow><mi>x</mi></mrow>`), else `n` itself.
const Node &unwrapped(const Node &n)
{
    if ((n.name == u"mrow" || n.name == u"mstyle" || n.name == u"mpadded") && n.kids.size() == 1)
        return unwrapped(n.kids.first());
    return n;
}

/// Does a script need braces around this base (`{a+b}^2`)?
bool needsBraces(const Node &base)
{
    const Node &n = unwrapped(base);
    if (isToken(n))
        return !isAtomicTex(texOf(n));
    if (n.name == u"mfrac" || n.name == u"msqrt" || n.name == u"mroot" || n.name == u"mfenced" || n.name == u"mtable")
        return false;
    if (n.name == u"mrow" && n.kids.size() >= 2) {
        const Node &first = n.kids.first();
        const Node &last = n.kids.last();
        if (first.name == u"mo" && last.name == u"mo" && isOpeningFence(tokenText(first))
            && isClosingFence(tokenText(last)))
            return false;
    }
    return true;
}

/// A big operator or a function name that keeps its limits above and below: `\sum`, `\lim`.
bool isLimitOperator(const Node &base)
{
    const Node &n = unwrapped(base);
    const QString t = tokenText(n);
    static const QSet<QString> operators = {u"∑"_s, u"∏"_s, u"∐"_s, u"∫"_s, u"∬"_s, u"∭"_s, u"∮"_s,
                                            u"⋃"_s, u"⋂"_s, u"⋁"_s, u"⋀"_s, u"⨁"_s, u"⨂"_s, u"⨀"_s};
    static const QSet<QString> names = {u"lim"_s, u"limsup"_s, u"liminf"_s, u"max"_s, u"min"_s,    u"sup"_s,
                                        u"inf"_s, u"det"_s,    u"gcd"_s,    u"Pr"_s,  u"argmax"_s, u"argmin"_s};
    return (n.name == u"mo" && operators.contains(t)) || (n.name == u"mi" && names.contains(t));
}

QString scriptsTex(const Node &base, const Node *sub, const Node *sup)
{
    QString b = texOf(base);
    if (b.isEmpty())
        b = u"{}"_s;
    else if (needsBraces(base))
        b = group(b);
    // Limits of a sum or an integral are in braces, so what follows them stays apart.
    const bool braced = isLimitOperator(base);
    if (sub)
        b += u'_' + scriptArg(texOf(*sub), braced);
    if (sup)
        b += u'^' + scriptArg(texOf(*sup), braced);
    return b;
}

QString variantCommand(const QString &variant)
{
    if (variant == u"bold")
        return u"\\mathbf"_s;
    if (variant == u"double-struck")
        return u"\\mathbb"_s;
    if (variant == u"script" || variant == u"bold-script")
        return u"\\mathcal"_s;
    if (variant == u"fraktur" || variant == u"bold-fraktur")
        return u"\\mathfrak"_s;
    if (variant == u"sans-serif")
        return u"\\mathsf"_s;
    if (variant == u"monospace")
        return u"\\mathtt"_s;
    if (variant == u"bold-italic")
        return u"\\boldsymbol"_s;
    return {};
}

bool isAsciiWord(const QString &t)
{
    return std::ranges::all_of(t, [](QChar c) { return c.unicode() < 128 && c.isLetterOrNumber(); });
}

QString identifierTex(const Node &n)
{
    const QString t = tokenText(n);
    if (t.isEmpty())
        return {};
    const QString variant = n.attr(u"mathvariant"_s).value_or(QString());
    const QString command = variantCommand(variant);
    if (t.toUcs4().size() == 1) {
        if (t[0].unicode() < 128 && t[0].isLetterOrNumber()) {
            if (!command.isEmpty())
                return command + group(t);
            if (variant == u"normal")
                return u"\\mathrm"_s + group(t);
        }
        return symbolTex(t);
    }
    static const QSet<QString> functions = {
            u"sin"_s,    u"cos"_s,  u"tan"_s,    u"cot"_s,    u"sec"_s,  u"csc"_s, u"arcsin"_s, u"arccos"_s,
            u"arctan"_s, u"sinh"_s, u"cosh"_s,   u"tanh"_s,   u"coth"_s, u"log"_s, u"ln"_s,     u"lg"_s,
            u"exp"_s,    u"lim"_s,  u"limsup"_s, u"liminf"_s, u"max"_s,  u"min"_s, u"sup"_s,    u"inf"_s,
            u"det"_s,    u"dim"_s,  u"gcd"_s,    u"deg"_s,    u"hom"_s,  u"ker"_s, u"arg"_s,    u"Pr"_s};
    if (functions.contains(t))
        return u'\\' + t;
    if (isAsciiWord(t))
        return (command.isEmpty() ? u"\\mathrm"_s : command) + group(t);
    return symbolTex(t);
}

QString numberTex(const Node &n)
{
    static const QRegularExpression comma(u"(?<=\\d),(?=\\d)"_s);
    QString t = tokenText(n);
    t.replace(comma, u"{,}"_s);
    return t;
}

QString spaceTex(const Node &n)
{
    const auto value = leadingNumber(n.attr(u"width"_s).value_or(QString()));
    if (!value)
        return u"\\,"_s;
    if (*value <= 0)
        return {};
    return *value >= 1 ? u"\\quad "_s : *value >= 0.5 ? u"\\ "_s : u"\\,"_s;
}

QString overAccent(const QString &accent, bool over)
{
    static const QHash<QString, QString> above = {
            {u"¯"_s, u"\\overline"_s}, {u"‾"_s, u"\\overline"_s},  {u"̅"_s, u"\\overline"_s}, {u"-"_s, u"\\overline"_s},
            {u"^"_s, u"\\hat"_s},      {u"ˆ"_s, u"\\hat"_s},       {u"~"_s, u"\\tilde"_s},   {u"˜"_s, u"\\tilde"_s},
            {u"∼"_s, u"\\tilde"_s},    {u"→"_s, u"\\vec"_s},       {u"⃗"_s, u"\\vec"_s},      {u"˙"_s, u"\\dot"_s},
            {u"¨"_s, u"\\ddot"_s},     {u"ˇ"_s, u"\\check"_s},     {u"˘"_s, u"\\breve"_s},   {u"´"_s, u"\\acute"_s},
            {u"`"_s, u"\\grave"_s},    {u"⏞"_s, u"\\overbrace"_s}, {u"°"_s, u"\\mathring"_s}};
    static const QHash<QString, QString> below = {{u"_"_s, u"\\underline"_s},
                                                  {u"̲"_s, u"\\underline"_s},
                                                  {u"¯"_s, u"\\underline"_s},
                                                  {u"‾"_s, u"\\underline"_s},
                                                  {u"⏟"_s, u"\\underbrace"_s}};
    return (over ? above : below).value(accent);
}

QString underOverTex(const Node &n)
{
    const QList<Node> &k = n.kids;
    if (k.size() < 2)
        return rowOf(k);
    const bool hasUnder = n.name == u"munder" || n.name == u"munderover";
    const bool hasOver = n.name == u"mover" || n.name == u"munderover";
    const Node &base = k[0];
    const Node *under = hasUnder ? &k[1] : nullptr;
    const Node *over = hasOver ? &k[hasUnder ? 2 : 1] : nullptr;
    if (hasOver && !hasUnder && over->name == u"mo") {
        const QString command = overAccent(tokenText(*over), true);
        if (!command.isEmpty() && !isLimitOperator(base))
            return command + group(texOf(base));
    }
    if (hasUnder && !hasOver && under->name == u"mo") {
        const QString command = overAccent(tokenText(*under), false);
        if (!command.isEmpty() && !isLimitOperator(base))
            return command + group(texOf(base));
    }
    if (isLimitOperator(base))
        return scriptsTex(base, under, over);
    QString out = texOf(base);
    if (under)
        out = u"\\underset"_s + group(texOf(*under)) + group(out);
    if (over)
        out = u"\\overset"_s + group(texOf(*over)) + group(out);
    return out;
}

QString tableTex(const Node &table, const QString &environment)
{
    QStringList rows;
    for (const Node &row : table.kids) {
        if (row.name != u"mtr" && row.name != u"mlabeledtr")
            continue;
        QStringList cells;
        bool label = row.name == u"mlabeledtr";
        for (const Node &cell : row.kids) {
            if (label) {
                label = false;
                continue;
            }
            cells.append(texOf(cell));
        }
        rows.append(cells.join(u" & "_s));
    }
    if (rows.isEmpty())
        return {};
    return u"\\begin{"_s + environment + u"} "_s + rows.join(u" \\\\ "_s) + u" \\end{"_s + environment + u'}';
}

QString matrixEnvironment(const QString &open, const QString &close)
{
    if (open == u"(" && close == u")")
        return u"pmatrix"_s;
    if (open == u"[" && close == u"]")
        return u"bmatrix"_s;
    if (open == u"{" && close == u"}")
        return u"Bmatrix"_s;
    if (open == u"|" && close == u"|")
        return u"vmatrix"_s;
    if ((open == u"‖" || open == u"∥") && open == close)
        return u"Vmatrix"_s;
    return {};
}

QString rowOf(const QList<Node> &kids)
{
    QList<const Node *> items;
    for (const Node &k : kids) {
        if (k.name != u"annotation" && k.name != u"annotation-xml")
            items.append(&k);
    }
    if (items.size() >= 2 && items[0]->name == u"mo" && items[1]->name == u"mtable") {
        const QString open = tokenText(*items[0]);
        if (items.size() == 2 && open == u"{")
            return tableTex(*items[1], u"cases"_s);
        if (items.size() == 3 && items[2]->name == u"mo") {
            const QString environment = matrixEnvironment(open, tokenText(*items[2]));
            if (!environment.isEmpty())
                return tableTex(*items[1], environment);
        }
    }
    QString out;
    for (qsizetype k = 0; k < items.size(); ++k) {
        const Node &item = *items[k];
        if (item.name == u"mo" && tokenText(item) == u"√" && k + 1 < items.size()) {
            out = joinTex(out, u"\\sqrt"_s + group(texOf(*items[k + 1])));
            ++k;
            continue;
        }
        out = joinTex(out, texOf(item));
    }
    return out;
}

QString texOf(const Node &n)
{
    const QString &name = n.name;
    if (name == u"mi")
        return identifierTex(n);
    if (name == u"mn")
        return numberTex(n);
    if (name == u"mo")
        return symbolTex(tokenText(n));
    if (name == u"mtext" || name == u"ms") {
        const QString collapsed = collapsedSpaces(n.text);
        if (collapsed.isEmpty())
            return {};
        return collapsed.trimmed().isEmpty() ? u"\\ "_s : textCommand(collapsed);
    }
    if (name == u"mspace")
        return spaceTex(n);
    if (name == u"annotation" || name == u"annotation-xml" || name == u"mprescripts" || name == u"none")
        return {};
    if (name == u"mfrac" && n.kids.size() >= 2) {
        const auto thickness = leadingNumber(n.attr(u"linethickness"_s).value_or(QString()));
        const bool none = thickness && *thickness == 0.0;
        return (none ? u"\\binom"_s : u"\\frac"_s) + group(texOf(n.kids[0])) + group(texOf(n.kids[1]));
    }
    if (name == u"msqrt")
        return u"\\sqrt"_s + group(rowOf(n.kids));
    if (name == u"mroot" && n.kids.size() >= 2)
        return u"\\sqrt["_s + texOf(n.kids[1]) + u"]"_s + group(texOf(n.kids[0]));
    if ((name == u"msub" || name == u"msup") && n.kids.size() >= 2)
        return scriptsTex(n.kids[0], name == u"msub" ? &n.kids[1] : nullptr, name == u"msup" ? &n.kids[1] : nullptr);
    if (name == u"msubsup" && n.kids.size() >= 3)
        return scriptsTex(n.kids[0], &n.kids[1], &n.kids[2]);
    if (name == u"munder" || name == u"mover" || name == u"munderover")
        return underOverTex(n);
    if (name == u"semantics" || name == u"maction") {
        for (const Node &k : n.kids) {
            if (k.name != u"annotation" && k.name != u"annotation-xml")
                return texOf(k);
        }
        return {};
    }
    if (name == u"mphantom")
        return u"\\phantom"_s + group(rowOf(n.kids));
    if (name == u"mfenced") {
        const QString open = n.attr(u"open"_s).value_or(u"("_s).trimmed();
        const QString close = n.attr(u"close"_s).value_or(u")"_s).trimmed();
        const QString separators = n.attr(u"separators"_s).value_or(u","_s).simplified().remove(u' ');
        QString out = symbolTex(open);
        for (qsizetype k = 0; k < n.kids.size(); ++k) {
            if (k > 0 && !separators.isEmpty())
                out += symbolTex(QString(separators[std::min<qsizetype>(k - 1, separators.size() - 1)]));
            out = joinTex(out, texOf(n.kids[k]));
        }
        return joinTex(out, symbolTex(close));
    }
    if (name == u"mtable")
        return tableTex(n, u"matrix"_s);
    // mrow, math, mstyle, mpadded, merror, menclose and what is unknown: the content.
    if (n.kids.isEmpty()) {
        const QString t = simplified(n.text);
        return t.isEmpty() ? QString() : textCommand(t);
    }
    return rowOf(n.kids);
}

/// `{\displaystyle …}` / `{\textstyle …}` around a whole formula (Wikipedia writes every formula
/// that way) taken off.
QString withoutStyleGroup(const QString &tex)
{
    static const QRegularExpression style(u"^\\{\\\\(?:display|text|scriptscript|script)style(?![A-Za-z])"_s);
    if (!tex.endsWith(u'}') || !style.match(tex).hasMatch())
        return tex;
    int depth = 0;
    for (qsizetype i = 0; i < tex.size(); ++i) {
        const QChar c = tex[i];
        if (c == u'\\') {
            ++i;
        } else if (c == u'{') {
            ++depth;
        } else if (c == u'}' && --depth == 0) {
            if (i != tex.size() - 1)
                return tex;
            break;
        }
    }
    const qsizetype close = tex.size() - 1;
    const QRegularExpressionMatch m = style.match(tex);
    return tex.sliced(m.capturedLength(), close - m.capturedLength()).trimmed();
}

/// A formula's TeX as written on one line: whitespace runs (line breaks too) are one space, the
/// style group around it is gone.
QString cleanTex(const QString &tex)
{
    return withoutStyleGroup(simplified(tex));
}

// --- the tree of a MathML element ----------------------------------------------------------------

class TreeBuilder {
public:
    TreeBuilder(const QString &html, const QList<Tag> &tags) : m_html(html), m_tags(tags) { }

    /// The element of the matched start tag `j`, its content read as far as the limits allow.
    Node build(int j)
    {
        m_budget = MaxMathNodes;
        return element(j, 0);
    }

    /// The text between the tags `from` and `to`, markup left out.
    QString textBetween(int from, int to) const
    {
        QString out;
        for (int k = from; k < to; ++k)
            out += QStringView(m_html).sliced(m_tags[k].end, m_tags[k + 1].start - m_tags[k].end);
        return out;
    }

private:
    Node leaf(const Tag &t) const
    {
        Node n;
        n.name = localName(t.name);
        n.attrs = m_html.sliced(t.attrFrom, t.attrTo - t.attrFrom);
        return n;
    }

    Node element(int j, int depth)
    {
        const Tag &t = m_tags[j];
        Node n = leaf(t);
        const int stop = t.match;
        if (depth >= MaxMathDepth || --m_budget < 0) {
            n.text = mathText(textBetween(j, stop));
            return n;
        }
        QString raw;
        qsizetype pos = t.end;
        for (int k = j + 1; k < stop;) {
            const Tag &c = m_tags[k];
            raw += QStringView(m_html).sliced(pos, c.start - pos);
            pos = c.end;
            if (c.closing) {
                ++k;
            } else if (c.leaf || c.match < 0 || c.match > stop) {
                n.kids.append(leaf(c));
                ++k;
            } else {
                n.kids.append(element(k, depth + 1));
                pos = m_tags[c.match].end;
                k = c.match + 1;
            }
        }
        raw += QStringView(m_html).sliced(pos, m_tags[stop].start - pos);
        n.text = mathText(raw);
        return n;
    }

    const QString &m_html;
    const QList<Tag> &m_tags;
    int m_budget = MaxMathNodes;
};

// --- finding the formulas ------------------------------------------------------------------------

class Extractor {
public:
    explicit Extractor(const QString &html) : m_html(html), m_tags(tokenize(html)), m_tree(html, m_tags) { }

    MathExtracted run()
    {
        for (int i = 0; i < m_tags.size(); ++i) {
            const Tag &t = m_tags[i];
            if (t.closing || t.leaf || t.match < 0)
                continue;
            const int end = t.match;
            const QString &name = t.name;
            if (name == u"script") {
                handleSource(i, false);
                i = end;
            } else if (name == u"brulion-math-source") {
                if (handleSource(i, true))
                    i = end;
            } else if (name == u"pre" || name == u"code" || name == u"textarea" || name == u"style"
                       || name == u"title") {
                i = end;
            } else if (localName(name) == u"math") {
                if (const QString tex = mathElementTex(i); !tex.isEmpty() && add(i, end, tex, displayOf(i)))
                    i = end;
            } else if (name == u"mjx-container") {
                if (handleMathJax3(i))
                    i = end;
            } else if ((name == u"span" || name == u"div") && mayBeFormula(t)) {
                if (handleContainer(i))
                    i = end;
            }
        }
        MathExtracted out;
        if (m_reps.isEmpty()) {
            out.html = m_html;
            return out;
        }
        QString html;
        html.reserve(m_html.size());
        qsizetype last = 0;
        for (const Rep &r : std::as_const(m_reps)) {
            html += QStringView(m_html).sliced(last, r.from - last);
            html += r.text;
            last = r.to;
        }
        html += QStringView(m_html).sliced(last);
        out.html = html;
        out.formulas = m_formulas;
        return out;
    }

private:
    struct Rep {
        qsizetype from = 0;
        qsizetype to = 0;
        QString text;
    };
    struct Found {
        QString tex;
        bool display = false;
    };

    QString attrsOf(const Tag &t) const { return m_html.sliced(t.attrFrom, t.attrTo - t.attrFrom); }

    std::optional<QString> attrOf(const Tag &t, const QString &name) const { return htmlAttribute(attrsOf(t), name); }

    QStringList classesOf(const Tag &t) const
    {
        return attrOf(t, u"class"_s).value_or(QString()).split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    }

    bool mayBeFormula(const Tag &t) const
    {
        const QStringView attrs = QStringView(m_html).sliced(t.attrFrom, t.attrTo - t.attrFrom);
        return attrs.contains(u"math", Qt::CaseInsensitive) || attrs.contains(u"katex", Qt::CaseInsensitive)
               || attrs.contains(u"mjx", Qt::CaseInsensitive);
    }

    QString contentOf(int i) const
    {
        const Tag &t = m_tags[i];
        return m_html.sliced(t.end, m_tags[t.match].start - t.end);
    }

    /// Replaces the element between the start tag `from` and the end tag `to` by the formula.
    bool add(int from, int to, const QString &rawTex, bool display)
    {
        const QString tex = cleanTex(rawTex);
        if (tex.isEmpty() || m_formulas.size() >= MaxFormulas)
            return false;
        const QString slot = QChar(MathSlotOpen) + QString::number(m_formulas.size()) + QChar(MathSlotClose);
        m_formulas.append({tex, display});
        m_reps.append({m_tags[from].start, m_tags[to].end, display ? u"<p>"_s + slot + u"</p>"_s : slot});
        return true;
    }

    void drop(int from, int to) { m_reps.append({m_tags[from].start, m_tags[to].end, QString()}); }

    bool displayOf(int mathTag) const
    {
        return attrOf(m_tags[mathTag], u"display"_s).value_or(QString()).compare(u"block"_s, Qt::CaseInsensitive) == 0;
    }

    /// The TeX of the `<math>` element at tag `j`: the annotation of the TeX source, else the
    /// MathML converted, else the `alttext`.
    QString mathElementTex(int j)
    {
        const int end = m_tags[j].match;
        for (int k = j + 1; k < end; ++k) {
            const Tag &c = m_tags[k];
            if (c.closing || c.match < 0 || localName(c.name) != u"annotation")
                continue;
            const QString encoding = attrOf(c, u"encoding"_s).value_or(QString()).toLower();
            if (encoding.contains(u"tex")) {
                if (const QString tex = cleanTex(mathText(contentOf(k))); !tex.isEmpty())
                    return tex;
            }
        }
        QString tex = cleanTex(texOf(m_tree.build(j)));
        if (tex.isEmpty()) {
            tex = cleanTex(mathText(attrOf(m_tags[j], u"alttext"_s).value_or(QString())));
            if (tex.isEmpty())
                tex = textCommandOf(m_tree.textBetween(j, end));
        }
        return tex;
    }

    static QString textCommandOf(const QString &text)
    {
        const QString t = simplified(mathText(text));
        return t.isEmpty() ? QString() : textCommand(t);
    }

    /// The first formula inside the element between the start tag `from` and the end tag `to`.
    std::optional<Found> fromRange(int from, int to)
    {
        for (int k = from + 1; k < to; ++k) {
            const Tag &c = m_tags[k];
            if (c.closing || c.leaf || c.match < 0 || localName(c.name) != u"math")
                continue;
            if (const QString tex = mathElementTex(k); !tex.isEmpty())
                return Found{tex, displayOf(k)};
            k = c.match;
        }
        return std::nullopt;
    }

    static bool hasClass(const QStringList &classes, const QString &name) { return classes.contains(name); }

    /// MathJax 2 and 3 keep the TeX of a formula in a `<script type="math/tex">` (mode=display
    /// for a block) or `type="math/mml"` of MathML next to the rendering; the extension's
    /// `<brulion-math-source type="…">` is the same where a page reader removed the scripts.
    bool handleSource(int i, bool escaped)
    {
        const Tag &t = m_tags[i];
        const QString type = attrOf(t, u"type"_s).value_or(QString()).toLower();
        if (!type.startsWith(u"math/"))
            return false;
        const QString content = escaped ? decodeHtmlEntities(contentOf(i)) : contentOf(i);
        if (type.startsWith(u"math/tex") || type.startsWith(u"math/latex"))
            return add(i, t.match, content, type.contains(u"mode=display"));
        if (type.startsWith(u"math/mml")) {
            static const QRegularExpression block(u"display\\s*=\\s*[\"']block[\"']"_s);
            return add(i, t.match, mathmlToTex(content), block.match(content).hasMatch());
        }
        return false;
    }

    bool handleMathJax3(int i)
    {
        const Tag &t = m_tags[i];
        std::optional<Found> found = fromRange(i, t.match);
        if (!found) {
            for (const QString &name : {u"data-tex"_s, u"data-latex"_s, u"data-formula"_s}) {
                if (const auto value = attrOf(t, name); value && !value->trimmed().isEmpty()) {
                    found = Found{*value, false};
                    break;
                }
            }
        }
        if (!found)
            return false;
        const bool display = attrOf(t, u"display"_s).value_or(QString()).compare(u"true"_s, Qt::CaseInsensitive) == 0;
        return add(i, t.match, found->tex, display || found->display);
    }

    /// After the element at start tag `i`: only blanks and elements of MathJax 2 up to a math script?
    bool followedByMathScript(int i) const
    {
        int j = m_tags[i].match + 1;
        qsizetype pos = m_tags[m_tags[i].match].end;
        for (int guard = 0; j < m_tags.size() && guard < 8; ++guard) {
            const Tag &c = m_tags[j];
            if (!QStringView(m_html).sliced(pos, c.start - pos).trimmed().isEmpty() || c.closing)
                return false;
            if (c.name == u"script" || c.name == u"brulion-math-source")
                return attrOf(c, u"type"_s).value_or(QString()).toLower().startsWith(u"math/");
            if ((c.name != u"span" && c.name != u"div") || c.match < 0 || !isMathJax2(classesOf(c)))
                return false;
            pos = m_tags[c.match].end;
            j = c.match + 1;
        }
        return false;
    }

    static bool isMathJax2(const QStringList &classes)
    {
        return std::ranges::any_of(classes, [](const QString &c) {
            return c == u"MathJax" || c.startsWith(u"MathJax_") || c == u"MJX_Assistive_MathML";
        });
    }

    /// KaTeX (`.katex-display`, `.katex`), Wikipedia (`.mwe-math-element`), MathJax 2's rendering
    /// (`.MathJax`, `.MathJax_Preview`…) and Pandoc's and Sphinx's `.math`.
    bool handleContainer(int i)
    {
        const Tag &t = m_tags[i];
        const int end = t.match;
        const QStringList classes = classesOf(t);
        if (hasClass(classes, u"katex-display"_s) || hasClass(classes, u"katex"_s)
            || hasClass(classes, u"katex-mathml"_s)) {
            const auto found = fromRange(i, end);
            return found && add(i, end, found->tex, hasClass(classes, u"katex-display"_s) || found->display);
        }
        if (hasClass(classes, u"mwe-math-element"_s)) {
            std::optional<Found> found = fromRange(i, end);
            bool display = hasClass(classes, u"mwe-math-element-block"_s) || (found && found->display);
            if (!found) {
                for (int k = i + 1; k < end && !found; ++k) {
                    const Tag &c = m_tags[k];
                    if (c.closing || c.name != u"img")
                        continue;
                    const QString cls = attrOf(c, u"class"_s).value_or(QString());
                    const QString alt = attrOf(c, u"alt"_s).value_or(QString());
                    if (cls.contains(u"mwe-math-fallback"_s) && !alt.trimmed().isEmpty()) {
                        found = Found{alt, false};
                        display = display || cls.contains(u"display"_s) || cls.contains(u"block"_s);
                    }
                }
            }
            return found && add(i, end, found->tex, display);
        }
        if (isMathJax2(classes)) {
            if (followedByMathScript(i)) {
                drop(i, end);
                return true;
            }
            const auto found = fromRange(i, end);
            return found
                   && add(i, end, found->tex,
                          hasClass(classes, u"MathJax_Display"_s) || hasClass(classes, u"MathJax_SVG_Display"_s)
                                  || found->display);
        }
        if (hasClass(classes, u"math"_s))
            return handleDelimited(i);
        return false;
    }

    /// `<span class="math inline">\(x\)</span>` and `<div class="math display">\[x\]</div>` (Pandoc,
    /// Sphinx): a formula only when its whole text is in the delimiters.
    bool handleDelimited(int i)
    {
        const int end = m_tags[i].match;
        const QString text = decodeHtmlEntities(m_tree.textBetween(i, end)).trimmed();
        const auto wrapped = [&](QStringView open, QStringView close) {
            return text.size() > open.size() + close.size() && text.startsWith(open) && text.endsWith(close);
        };
        if (wrapped(u"\\(", u"\\)"))
            return add(i, end, text.sliced(2, text.size() - 4), false);
        if (wrapped(u"\\[", u"\\]"))
            return add(i, end, text.sliced(2, text.size() - 4), true);
        if (wrapped(u"$$", u"$$"))
            return add(i, end, text.sliced(2, text.size() - 4), true);
        if (wrapped(u"$", u"$") && !text.sliced(1, text.size() - 2).contains(u'$'))
            return add(i, end, text.sliced(1, text.size() - 2), false);
        return false;
    }

    const QString &m_html;
    QList<Tag> m_tags;
    TreeBuilder m_tree;
    QList<Rep> m_reps;
    QList<MathFormula> m_formulas;
};

} // namespace

MathExtracted extractMath(const QString &html)
{
    // Quick exit for the pages without a trace of math (the most of them).
    if (!html.contains(u"math"_s, Qt::CaseInsensitive) && !html.contains(u"katex"_s, Qt::CaseInsensitive)
        && !html.contains(u"mjx"_s, Qt::CaseInsensitive)) {
        MathExtracted out;
        out.html = html;
        return out;
    }
    return Extractor(html).run();
}

QString mathmlToTex(const QString &mathml)
{
    const QList<Tag> tags = tokenize(mathml);
    for (int i = 0; i < tags.size(); ++i) {
        if (tags[i].closing || localName(tags[i].name) != u"math" || tags[i].match < 0)
            continue;
        TreeBuilder tree(mathml, tags);
        QString tex = cleanTex(texOf(tree.build(i)));
        if (tex.isEmpty()) {
            const QString text = simplified(mathText(tree.textBetween(i, tags[i].match)));
            tex = text.isEmpty() ? QString() : textCommand(text);
        }
        return tex;
    }
    return {};
}

} // namespace brulion::io::detail
