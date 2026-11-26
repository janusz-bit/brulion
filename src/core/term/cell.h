// The terminal's cells and lines (term.h): what one grid position holds — a code point (or, with
// combining marks joined to it, a short cluster kept by its line), its width, colours, attributes
// and hyperlink — and a line of them.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace brulion::term {

/// A cell's colour: the default (the view's foreground or background), one of the 256 indexed
/// colours (0-15: the view's ANSI palette, 16-231: xterm's 6x6x6 cube, 232-255: its greys) or 24-bit
/// RGB. Four bytes: the kind in the top byte.
class Color {
public:
    enum Kind : quint8 { Default = 0, Indexed = 1, Rgb = 2 };

    constexpr Color() = default;
    static constexpr Color indexed(int index) { return Color((quint32(Indexed) << 24) | quint32(index & 0xff)); }
    static constexpr Color rgb(int r, int g, int b)
    {
        return Color((quint32(Rgb) << 24) | (quint32(r & 0xff) << 16) | (quint32(g & 0xff) << 8) | quint32(b & 0xff));
    }

    constexpr Kind kind() const { return Kind(m_value >> 24); }
    constexpr bool isDefault() const { return m_value == 0; }
    /// The palette index (kind() == Indexed).
    constexpr int index() const { return int(m_value & 0xff); }
    /// 0xRRGGBB (kind() == Rgb).
    constexpr quint32 rgbValue() const { return m_value & 0xffffff; }
    constexpr quint32 raw() const { return m_value; }

    friend constexpr bool operator==(Color a, Color b) { return a.m_value == b.m_value; }
    friend constexpr bool operator!=(Color a, Color b) { return a.m_value != b.m_value; }

private:
    explicit constexpr Color(quint32 value) : m_value(value) { }
    quint32 m_value = 0;
};

/// SGR attributes of a cell (Cell::attrs, Pen::attrs).
enum Attribute : quint16 {
    Bold = 0x0001,
    Dim = 0x0002,
    Italic = 0x0004,
    Underline = 0x0008,
    DoubleUnderline = 0x0010,
    Blink = 0x0020,
    Inverse = 0x0040,
    Invisible = 0x0080,
    Strikethrough = 0x0100,
    Overline = 0x0200,
    /// The SGR attributes (the rest are the cell's own flags).
    AttributeMask = 0x0fff,
    /// The cell holds the left half of a two-column character; the next cell is its right half
    /// (ch 0).
    WideFlag = 0x4000,
    /// More code points than `ch` (combining marks): Line::clusters holds the cell's whole text.
    ClusterFlag = 0x8000,
};

/// What printed characters get: the current SGR state and hyperlink.
struct Pen {
    Color fg;
    Color bg;
    quint16 attrs = 0;
    /// The OSC 8 hyperlink (Screen::hyperlink()), 0: none.
    quint16 link = 0;

    friend bool operator==(const Pen &, const Pen &) = default;
};

/// One grid position, 16 bytes.
struct Cell {
    /// The code point; 0 for the right half of a two-column character.
    char32_t ch = U' ';
    Color fg;
    Color bg;
    /// Attribute flags.
    quint16 attrs = 0;
    /// The OSC 8 hyperlink (Screen::hyperlink()), 0: none.
    quint16 link = 0;

    /// A blank cell erased with `pen`'s background (xterm's "background colour erase").
    static Cell blank(const Pen &pen)
    {
        Cell c;
        c.bg = pen.bg;
        return c;
    }
    bool isWideTail() const { return ch == 0; }
    bool isWide() const { return attrs & WideFlag; }
    /// A space with default colours and nothing else: what trailing blanks of a line are.
    bool isPlainBlank() const { return ch == U' ' && fg.isDefault() && bg.isDefault() && attrs == 0 && link == 0; }

    friend bool operator==(const Cell &, const Cell &) = default;
};

/// One line of the grid or of the scrollback. A scrollback line may be shorter than the screen
/// (trailing plain blanks are dropped) or longer (the screen got narrower since).
struct Line {
    QList<Cell> cells;
    /// The whole text of cells with ClusterFlag (a code point and the combining marks after it), by
    /// column.
    QHash<int, QString> clusters;
    /// The line goes on in the next one: the text wrapped at the right margin (copying joins them).
    bool wrapped = false;

    /// The text of the cell at `column`: its cluster, its code point, "" for a wide character's right
    /// half.
    QString cellText(int column) const;
    /// The text of columns [from, to) — wide characters' right halves skipped, trailing spaces kept.
    QString text(int from, int to) const;
    /// The text of the whole line without trailing spaces.
    QString trimmedText() const;
};

} // namespace brulion::term

// Relocatable, not primitive: QList value-initialises primitive types with memset (ch would be 0).
Q_DECLARE_TYPEINFO(brulion::term::Cell, Q_RELOCATABLE_TYPE);
Q_DECLARE_TYPEINFO(brulion::term::Line, Q_RELOCATABLE_TYPE);
