#include "term/parser.h"

namespace brulion::term {

namespace {

constexpr char32_t Replacement = 0xFFFD;
constexpr int MaxParamValue = 65535;

} // namespace

void Parser::feed(QByteArrayView data)
{
    const auto *p = reinterpret_cast<const unsigned char *>(data.data());
    const auto *end = p + data.size();
    while (p < end) {
        // The common case: printable ASCII in the ground state.
        if (m_state == State::Ground && m_utf8Need == 0) {
            while (p < end && *p >= 0x20 && *p < 0x7f) {
                m_handler->print(char32_t(*p));
                ++p;
            }
            if (p == end)
                break;
        }
        byte(*p++);
    }
}

void Parser::reset()
{
    m_state = State::Ground;
    m_utf8Need = 0;
    clear();
    m_osc.clear();
}

void Parser::clear()
{
    m_params.count = 0;
    m_inNumber = false;
    m_afterSeparator = false;
    m_nextIsSub = false;
    m_paramsFull = false;
    m_intermediateCount = 0;
    m_tooManyIntermediates = false;
    m_prefix = 0;
}

void Parser::enter(State state)
{
    m_state = state;
    switch (state) {
    case State::Escape:
    case State::CsiEntry:
    case State::DcsEntry: clear(); break;
    case State::OscString: m_osc.clear(); break;
    default: break;
    }
}

void Parser::collect(unsigned char b)
{
    if (m_intermediateCount < 2)
        m_intermediates[m_intermediateCount++] = char(b);
    else
        m_tooManyIntermediates = true;
}

void Parser::param(unsigned char b)
{
    if (b == ';' || b == ':') {
        if (!m_inNumber) {
            // A parameter left out: "CSI ;5H".
            if (m_params.count < CsiParams::Max) {
                m_params.values[m_params.count] = -1;
                m_params.sub[m_params.count] = m_nextIsSub;
                ++m_params.count;
            }
        }
        m_inNumber = false;
        m_paramsFull = false;
        m_afterSeparator = true;
        m_nextIsSub = b == ':';
        return;
    }
    m_afterSeparator = false;
    if (!m_inNumber) {
        m_inNumber = true;
        if (m_params.count >= CsiParams::Max) {
            m_paramsFull = true;
            return;
        }
        m_params.values[m_params.count] = 0;
        m_params.sub[m_params.count] = m_nextIsSub;
        ++m_params.count;
    }
    if (m_paramsFull)
        return;
    int &value = m_params.values[m_params.count - 1];
    value = value * 10 + (b - '0');
    if (value > MaxParamValue)
        value = MaxParamValue;
}

void Parser::dispatchCsi(unsigned char final)
{
    if (m_tooManyIntermediates)
        return;
    if (m_afterSeparator && m_params.count < CsiParams::Max) {
        // A trailing separator: "CSI 5;H" has a second parameter left out.
        m_params.values[m_params.count] = -1;
        m_params.sub[m_params.count] = m_nextIsSub;
        ++m_params.count;
    }
    m_handler->csiDispatch(m_params, QByteArrayView(m_intermediates, m_intermediateCount), m_prefix, char(final));
}

void Parser::endOsc()
{
    m_handler->oscDispatch(m_osc);
    m_osc.clear();
}

void Parser::ground(unsigned char b)
{
    if (m_utf8Need > 0) {
        if (b >= m_utf8Lower && b <= m_utf8Upper) {
            m_utf8Code = (m_utf8Code << 6) | (b & 0x3f);
            m_utf8Lower = 0x80;
            m_utf8Upper = 0xbf;
            if (--m_utf8Need == 0)
                m_handler->print(m_utf8Code);
            return;
        }
        // Cut short: the bytes so far are one invalid subpart, `b` starts afresh.
        m_utf8Need = 0;
        m_handler->print(Replacement);
    }
    if (b < 0x20) {
        if (b == 0x1b)
            enter(State::Escape);
        else
            m_handler->execute(char(b));
        return;
    }
    if (b < 0x7f) {
        m_handler->print(char32_t(b));
        return;
    }
    if (b == 0x7f)
        return;
    m_utf8Lower = 0x80;
    m_utf8Upper = 0xbf;
    if (b >= 0xc2 && b <= 0xdf) {
        m_utf8Need = 1;
        m_utf8Code = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        m_utf8Need = 2;
        m_utf8Code = b & 0x0f;
        if (b == 0xe0)
            m_utf8Lower = 0xa0; // no overlong forms
        else if (b == 0xed)
            m_utf8Upper = 0x9f; // no surrogates
    } else if (b >= 0xf0 && b <= 0xf4) {
        m_utf8Need = 3;
        m_utf8Code = b & 0x07;
        if (b == 0xf0)
            m_utf8Lower = 0x90; // no overlong forms
        else if (b == 0xf4)
            m_utf8Upper = 0x8f; // nothing beyond U+10FFFF
    } else {
        // A continuation byte without a lead, C0 / C1 overlong leads, F5-FF.
        m_handler->print(Replacement);
    }
}

void Parser::byte(unsigned char b)
{
    if (m_state == State::Ground) {
        ground(b);
        return;
    }
    // Anywhere: CAN and SUB cancel, ESC starts a new sequence (and ends a string).
    if (b == 0x18 || b == 0x1a) {
        enter(State::Ground);
        return;
    }
    if (b == 0x1b) {
        if (m_state == State::OscString)
            endOsc();
        enter(State::Escape);
        return;
    }

    switch (m_state) {
    case State::Ground: break;
    case State::Escape:
        if (b < 0x20) {
            m_handler->execute(char(b));
            return;
        }
        if (b >= 0x80) {
            enter(State::Ground);
            ground(b);
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            m_state = State::EscapeIntermediate;
            return;
        }
        switch (b) {
        case 'P': enter(State::DcsEntry); return;
        case '[': enter(State::CsiEntry); return;
        case ']': enter(State::OscString); return;
        case 'X':
        case '^':
        case '_': enter(State::SosPmApcString); return;
        case 0x7f: return;
        default: break;
        }
        m_handler->escDispatch(QByteArrayView(), char(b));
        enter(State::Ground);
        return;
    case State::EscapeIntermediate:
        if (b < 0x20) {
            m_handler->execute(char(b));
            return;
        }
        if (b >= 0x80) {
            enter(State::Ground);
            ground(b);
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            return;
        }
        if (b == 0x7f)
            return;
        if (!m_tooManyIntermediates)
            m_handler->escDispatch(QByteArrayView(m_intermediates, m_intermediateCount), char(b));
        enter(State::Ground);
        return;
    case State::CsiEntry:
    case State::CsiParam:
        if (b < 0x20) {
            m_handler->execute(char(b));
            return;
        }
        if (b >= 0x80) {
            m_state = State::CsiIgnore;
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            m_state = State::CsiIntermediate;
            return;
        }
        if (b <= 0x3b) {
            param(b);
            m_state = State::CsiParam;
            return;
        }
        if (b <= 0x3f) {
            if (m_state == State::CsiEntry) {
                m_prefix = char(b);
                m_state = State::CsiParam;
            } else {
                m_state = State::CsiIgnore;
            }
            return;
        }
        if (b == 0x7f)
            return;
        dispatchCsi(b);
        enter(State::Ground);
        return;
    case State::CsiIntermediate:
        if (b < 0x20) {
            m_handler->execute(char(b));
            return;
        }
        if (b >= 0x80 || (b >= 0x30 && b <= 0x3f)) {
            m_state = State::CsiIgnore;
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            return;
        }
        if (b == 0x7f)
            return;
        dispatchCsi(b);
        enter(State::Ground);
        return;
    case State::CsiIgnore:
        if (b < 0x20) {
            m_handler->execute(char(b));
            return;
        }
        if (b >= 0x40 && b <= 0x7e)
            enter(State::Ground);
        return;
    case State::DcsEntry:
    case State::DcsParam:
        if (b < 0x20 || b == 0x7f)
            return;
        if (b >= 0x80) {
            m_state = State::DcsIgnore;
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            m_state = State::DcsIntermediate;
            return;
        }
        if (b <= 0x3b) {
            m_state = State::DcsParam;
            return;
        }
        if (b <= 0x3f) {
            m_state = m_state == State::DcsEntry ? State::DcsParam : State::DcsIgnore;
            return;
        }
        m_state = State::DcsPassthrough;
        return;
    case State::DcsIntermediate:
        if (b < 0x20 || b == 0x7f)
            return;
        if (b >= 0x80 || (b >= 0x30 && b <= 0x3f)) {
            m_state = State::DcsIgnore;
            return;
        }
        if (b <= 0x2f) {
            collect(b);
            return;
        }
        m_state = State::DcsPassthrough;
        return;
    case State::DcsPassthrough:
    case State::DcsIgnore:
    case State::SosPmApcString:
        // Consumed until ST (ESC, above): DCS requests (DECRQSS, XTGETTCAP, sixel) are not answered.
        return;
    case State::OscString:
        if (b == 0x07) {
            endOsc();
            enter(State::Ground);
            return;
        }
        if (b < 0x20)
            return;
        if (m_osc.size() < MaxOscLength)
            m_osc.append(char(b));
        return;
    }
}

} // namespace brulion::term
