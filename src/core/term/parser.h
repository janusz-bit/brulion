// The escape sequence parser (term.h): a byte stream from the program into actions — the state
// machine of Paul Williams' DEC-compatible parser (vt100.net/emu/dec_ansi_parser: ground, escape,
// escape intermediate, CSI entry / param / intermediate / ignore, DCS entry / param / intermediate /
// passthrough / ignore, OSC string, SOS/PM/APC string), for a UTF-8 terminal:
//
// * UTF-8 is decoded in the ground state only, robust to sequences split across feed() calls; an
//   invalid byte, an overlong form, a surrogate, a code point beyond U+10FFFF or a sequence cut
//   short by another byte gives U+FFFD (one per maximal invalid subpart, as the Unicode Standard
//   recommends). 8-bit C1 controls are not recognised (in UTF-8 they are part of characters).
// * CSI parameters: up to 32, each saturating at 65535; ':' separates sub-parameters (SGR's
//   38:2::r:g:b). More parameters are dropped, the sequence still dispatched.
// * OSC strings end with BEL or ST (ESC \) and are cut at 4096 bytes; DCS strings and SOS / PM / APC
//   strings are consumed and ignored.
// * CAN and SUB cancel a sequence; ESC starts a new one anywhere; C0 controls inside a CSI are
//   executed as they come.
#pragma once

#include <QByteArray>
#include <QByteArrayView>

namespace brulion::term {

/// A CSI's parameters as received: every number in order, -1 where one was left out, and whether
/// it followed a ':' (a sub-parameter of the one before it).
struct CsiParams {
    static constexpr int Max = 32;
    int values[Max] = {};
    bool sub[Max] = {};
    int count = 0;

    /// Parameter `i` (counting sub-parameters too), `fallback` when missing or 0 if `zeroIsDefault`.
    int at(int i, int fallback, bool zeroIsDefault = true) const
    {
        if (i < 0 || i >= count || values[i] < 0 || (zeroIsDefault && values[i] == 0))
            return fallback;
        return values[i];
    }
};

/// What the parser recognises, in order. Intermediates are the bytes 0x20-0x2f (at most two);
/// `prefix` is a CSI's private marker ('?', '>', '<', '=') or 0.
class ParserHandler {
public:
    virtual ~ParserHandler() = default;
    virtual void print(char32_t cp) = 0;
    virtual void execute(char control) = 0;
    virtual void escDispatch(QByteArrayView intermediates, char final) = 0;
    virtual void csiDispatch(const CsiParams &params, QByteArrayView intermediates, char prefix, char final) = 0;
    /// An OSC string's bytes (UTF-8 as sent, cut at the limit).
    virtual void oscDispatch(const QByteArray &data) = 0;
};

class Parser {
public:
    explicit Parser(ParserHandler *handler) : m_handler(handler) { }

    void feed(QByteArrayView data);
    /// Back to the ground state, any partial sequence and UTF-8 character dropped.
    void reset();

    static constexpr int MaxOscLength = 4096;

private:
    enum class State : unsigned char {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        CsiParam,
        CsiIntermediate,
        CsiIgnore,
        DcsEntry,
        DcsParam,
        DcsIntermediate,
        DcsPassthrough,
        DcsIgnore,
        OscString,
        SosPmApcString,
    };

    void byte(unsigned char b);
    void ground(unsigned char b);
    void utf8(unsigned char b);
    void clear();
    void collect(unsigned char b);
    void param(unsigned char b);
    void dispatchCsi(unsigned char final);
    void endOsc();
    void enter(State state);

    ParserHandler *m_handler;
    State m_state = State::Ground;

    // UTF-8 decoding (ground state).
    int m_utf8Need = 0;
    char32_t m_utf8Code = 0;
    unsigned char m_utf8Lower = 0x80;
    unsigned char m_utf8Upper = 0xbf;

    CsiParams m_params;
    bool m_inNumber = false;       // digits of the current parameter came
    bool m_afterSeparator = false; // the last byte was ';' or ':'
    bool m_nextIsSub = false;      // the next parameter follows a ':'
    bool m_paramsFull = false;     // a parameter beyond CsiParams::Max is being dropped
    char m_intermediates[2] = {0, 0};
    int m_intermediateCount = 0;
    bool m_tooManyIntermediates = false;
    char m_prefix = 0;
    QByteArray m_osc;
};

} // namespace brulion::term
