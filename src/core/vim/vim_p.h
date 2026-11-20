// The Vim engine's key tokens (internal to the vim module): the engine turns every key into one
// character of a command's keys — a typed character as itself, Ctrl+letter as its control
// character (Ctrl+R = U+0012), the special keys as these.
#pragma once

#include <QChar>
#include <QString>

namespace brulion::vim::detail {

inline constexpr char16_t Esc = 0x1B;
inline constexpr char16_t Enter = u'\r';
inline constexpr char16_t Backspace = u'\b';
inline constexpr char16_t Delete = 0x7F;
inline constexpr char16_t Tab = u'\t';
// Keys without a character: private-use code points.
inline constexpr char16_t Left = 0xE001;
inline constexpr char16_t Right = 0xE002;
inline constexpr char16_t Up = 0xE003;
inline constexpr char16_t Down = 0xE004;
inline constexpr char16_t Home = 0xE005;
inline constexpr char16_t End = 0xE006;
inline constexpr char16_t PageUp = 0xE007;
inline constexpr char16_t PageDown = 0xE008;
inline constexpr char16_t Insert = 0xE009;

/// Ctrl + an ASCII letter.
constexpr char16_t ctrl(char letter)
{
    return char16_t(letter & 0x1F);
}

inline QString tok(char16_t c)
{
    return QString(QChar(c));
}

/// A token is one of the special keys above.
inline bool isSpecial(QChar c)
{
    return c.unicode() >= 0xE000 && c.unicode() <= 0xE0FF;
}

} // namespace brulion::vim::detail
