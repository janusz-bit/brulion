#include "canvas/json_p.h"

#include <QLocale>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvas::detail {

const JsonMember *JsonValue::member(QStringView key) const
{
    for (qsizetype i = members.size() - 1; i >= 0; --i) {
        if (members.at(i).key == key)
            return &members.at(i);
    }
    return nullptr;
}

namespace {

bool isSpace(QChar c)
{
    const char16_t u = c.unicode();
    return u == u' ' || u == u'\t' || u == u'\n' || u == u'\r';
}

int hexValue(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9')
        return u - u'0';
    if (u >= u'a' && u <= u'f')
        return u - u'a' + 10;
    if (u >= u'A' && u <= u'F')
        return u - u'A' + 10;
    return -1;
}

class Parser {
public:
    explicit Parser(QStringView text) : m_text(text) { }

    JsonDocument run()
    {
        JsonDocument doc;
        if (m_text.startsWith(QChar(0xFEFF)))
            m_pos = 1;
        skipSpace();
        doc.rootFrom = m_pos;
        doc.root = value(0);
        if (doc.root) {
            skipSpace();
            if (m_pos != m_text.size()) {
                doc.root.reset();
                m_error = JsonError::Syntax;
            }
        }
        doc.error = doc.root ? JsonError::None : (m_error == JsonError::None ? JsonError::Syntax : m_error);
        return doc;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && isSpace(m_text[m_pos]))
            ++m_pos;
    }

    bool fail(JsonError e = JsonError::Syntax)
    {
        if (m_error == JsonError::None)
            m_error = e;
        return false;
    }

    std::shared_ptr<JsonValue> value(int depth)
    {
        if (depth > MaxJsonDepth) {
            fail(JsonError::TooDeep);
            return nullptr;
        }
        if (++m_values > MaxJsonValues) {
            fail(JsonError::TooLarge);
            return nullptr;
        }
        if (m_pos >= m_text.size()) {
            fail();
            return nullptr;
        }
        auto v = std::make_shared<JsonValue>();
        v->from = m_pos;
        const QChar c = m_text[m_pos];
        bool ok = false;
        switch (c.unicode()) {
        case u'{': ok = object(*v, depth); break;
        case u'[': ok = array(*v, depth); break;
        case u'"':
            v->kind = JsonValue::Kind::String;
            ok = string(v->string);
            break;
        case u't': ok = literal(u"true", *v, JsonValue::Kind::Bool, true); break;
        case u'f': ok = literal(u"false", *v, JsonValue::Kind::Bool, false); break;
        case u'n': ok = literal(u"null", *v, JsonValue::Kind::Null, false); break;
        default: ok = number(*v); break;
        }
        if (!ok)
            return nullptr;
        v->to = m_pos;
        return v;
    }

    bool literal(QStringView word, JsonValue &v, JsonValue::Kind kind, bool b)
    {
        if (!m_text.sliced(m_pos).startsWith(word))
            return fail();
        m_pos += word.size();
        v.kind = kind;
        v.boolean = b;
        return true;
    }

    bool number(JsonValue &v)
    {
        const qsizetype start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == u'-')
            ++m_pos;
        const auto digits = [this] {
            const qsizetype s = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= u'0' && m_text[m_pos] <= u'9')
                ++m_pos;
            return m_pos - s;
        };
        if (digits() == 0)
            return fail();
        if (m_pos < m_text.size() && m_text[m_pos] == u'.') {
            ++m_pos;
            if (digits() == 0)
                return fail();
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == u'e' || m_text[m_pos] == u'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == u'+' || m_text[m_pos] == u'-'))
                ++m_pos;
            if (digits() == 0)
                return fail();
        }
        bool ok = false;
        const double d = QLocale::c().toDouble(m_text.sliced(start, m_pos - start), &ok);
        v.kind = JsonValue::Kind::Number;
        // An overflow (1e999) reads as infinity in JavaScript; here it is out of every limit.
        v.number = ok ? d : (m_text[start] == u'-' ? -HUGE_VAL : HUGE_VAL);
        return true;
    }

    bool string(QString &out)
    {
        ++m_pos; // the opening quote
        // Most strings have no escapes: one slice.
        const qsizetype start = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos] != u'"' && m_text[m_pos] != u'\\')
            ++m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == u'"') {
            out = m_text.sliced(start, m_pos - start).toString();
            ++m_pos;
            return true;
        }
        out = m_text.sliced(start, m_pos - start).toString();
        while (m_pos < m_text.size()) {
            const QChar c = m_text[m_pos];
            if (c == u'"') {
                ++m_pos;
                return true;
            }
            if (c != u'\\') {
                out += c;
                ++m_pos;
                continue;
            }
            if (++m_pos >= m_text.size())
                return fail();
            const QChar e = m_text[m_pos++];
            switch (e.unicode()) {
            case u'"': out += u'"'; break;
            case u'\\': out += u'\\'; break;
            case u'/': out += u'/'; break;
            case u'b': out += u'\b'; break;
            case u'f': out += u'\f'; break;
            case u'n': out += u'\n'; break;
            case u'r': out += u'\r'; break;
            case u't': out += u'\t'; break;
            case u'u': {
                if (m_pos + 4 > m_text.size())
                    return fail();
                int code = 0;
                for (int i = 0; i < 4; ++i) {
                    const int h = hexValue(m_text[m_pos + i]);
                    if (h < 0)
                        return fail();
                    code = code * 16 + h;
                }
                m_pos += 4;
                out += QChar(char16_t(code)); // (surrogate pairs come as two escapes)
                break;
            }
            default: return fail();
            }
        }
        return fail(); // no closing quote
    }

    bool array(JsonValue &v, int depth)
    {
        v.kind = JsonValue::Kind::Array;
        ++m_pos; // [
        qsizetype lead = m_pos;
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == u']') {
            v.closeFrom = lead;
            ++m_pos;
            return true;
        }
        for (;;) {
            std::shared_ptr<JsonValue> item = value(depth + 1);
            if (!item)
                return false;
            v.items.append(std::move(item));
            v.itemLeads.append(lead);
            const qsizetype afterItem = m_pos;
            skipSpace();
            if (m_pos >= m_text.size())
                return fail();
            if (m_text[m_pos] == u',') {
                ++m_pos;
                lead = m_pos;
                skipSpace();
                continue;
            }
            if (m_text[m_pos] == u']') {
                v.closeFrom = afterItem;
                ++m_pos;
                return true;
            }
            return fail();
        }
    }

    bool object(JsonValue &v, int depth)
    {
        v.kind = JsonValue::Kind::Object;
        ++m_pos; // {
        qsizetype lead = m_pos;
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == u'}') {
            v.closeFrom = lead;
            ++m_pos;
            return true;
        }
        for (;;) {
            if (m_pos >= m_text.size() || m_text[m_pos] != u'"')
                return fail();
            JsonMember m;
            m.leadFrom = lead;
            m.keyFrom = m_pos;
            if (!string(m.key))
                return false;
            m.keyTo = m_pos;
            skipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != u':')
                return fail();
            ++m_pos;
            skipSpace();
            m.value = value(depth + 1);
            if (!m.value)
                return false;
            v.members.append(std::move(m));
            const qsizetype afterValue = m_pos;
            skipSpace();
            if (m_pos >= m_text.size())
                return fail();
            if (m_text[m_pos] == u',') {
                ++m_pos;
                lead = m_pos;
                skipSpace();
                continue;
            }
            if (m_text[m_pos] == u'}') {
                v.closeFrom = afterValue;
                ++m_pos;
                return true;
            }
            return fail();
        }
    }

    QStringView m_text;
    qsizetype m_pos = 0;
    qsizetype m_values = 0;
    JsonError m_error = JsonError::None;
};

} // namespace

JsonDocument parseJson(QStringView text)
{
    return Parser(text).run();
}

QString jsonString(QStringView s)
{
    static constexpr char16_t hex[] = u"0123456789abcdef";
    QString out;
    out.reserve(s.size() + 2);
    out += u'"';
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        const char16_t u = c.unicode();
        switch (u) {
        case u'"': out += u"\\\""_s; continue;
        case u'\\': out += u"\\\\"_s; continue;
        case u'\b': out += u"\\b"_s; continue;
        case u'\f': out += u"\\f"_s; continue;
        case u'\n': out += u"\\n"_s; continue;
        case u'\r': out += u"\\r"_s; continue;
        case u'\t': out += u"\\t"_s; continue;
        default: break;
        }
        bool escape = u < 0x20;
        if (c.isHighSurrogate())
            escape = !(i + 1 < s.size() && s[i + 1].isLowSurrogate());
        else if (c.isLowSurrogate())
            escape = !(i > 0 && s[i - 1].isHighSurrogate());
        if (escape) {
            out += u"\\u"_s;
            out += QChar(hex[(u >> 12) & 0xf]);
            out += QChar(hex[(u >> 8) & 0xf]);
            out += QChar(hex[(u >> 4) & 0xf]);
            out += QChar(hex[u & 0xf]);
        } else {
            out += c;
        }
    }
    out += u'"';
    return out;
}

QString jsonNumber(double v)
{
    if (!std::isfinite(v))
        return u"0"_s;
    if (v == 0)
        return u"0"_s; // (also -0, as JSON.stringify)
    const double a = std::abs(v);
    if (a < 1e21 && std::floor(v) == v)
        return QString::number(v, 'f', 0);
    if (a >= 1e-6 && a < 1e21)
        return QString::number(v, 'f', QLocale::FloatingPointShortest);
    // JavaScript's exponent form: "1.5e-7", "1e+21".
    QString e = QString::number(v, 'e', QLocale::FloatingPointShortest);
    const qsizetype at = e.indexOf(u'e');
    if (at < 0)
        return e;
    QString mantissa = e.left(at);
    QString exponent = e.mid(at + 1);
    QChar sign = u'+';
    if (exponent.startsWith(u'-') || exponent.startsWith(u'+')) {
        sign = exponent.at(0);
        exponent.remove(0, 1);
    }
    while (exponent.size() > 1 && exponent.startsWith(u'0'))
        exponent.remove(0, 1);
    return mantissa + u'e' + sign + exponent;
}

} // namespace brulion::canvas::detail
