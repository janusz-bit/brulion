#include "ui/events.h"

namespace brulion::atrament {

namespace {

bool applyModifier(std::u16string_view name, Modifiers &m)
{
    if (name == u"ctrl" || name == u"control")
        m.control = true;
    else if (name == u"alt" || name == u"option")
        m.alt = true;
    else if (name == u"shift")
        m.shift = true;
    else if (name == u"super" || name == u"cmd" || name == u"win" || name == u"meta" || name == u"platform")
        m.platform = true;
    else if (name == u"fn")
        m.function = true;
    else
        return false;
    return true;
}

std::u16string lower(std::u16string_view s)
{
    std::u16string out(s);
    for (char16_t &c : out) {
        if (c >= u'A' && c <= u'Z')
            c = char16_t(c - u'A' + u'a');
    }
    return out;
}

} // namespace

std::optional<Keystroke> Keystroke::parse(std::u16string_view text)
{
    // Parts separated by '-'; a '-' that would start an empty part is the minus key itself:
    // "ctrl--" is ctrl and minus, "-" is minus, "ctrl-" has no key.
    std::vector<std::u16string> parts;
    std::u16string current;
    for (char16_t c : text) {
        if (c == u'-' && !current.empty()) {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (current.empty())
        return std::nullopt;
    parts.push_back(std::move(current));
    Keystroke k;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!applyModifier(lower(parts[i]), k.modifiers))
            return std::nullopt;
    }
    k.key = parts.back().size() == 1 ? parts.back() : lower(parts.back());
    if (k.key.size() == 1 && k.key[0] > u' ') {
        // A single character types itself (without Ctrl/Alt/Super); letters in lower case as keys.
        const char16_t c = k.key[0];
        if (c >= u'A' && c <= u'Z')
            k.key[0] = char16_t(c - u'A' + u'a');
        if (!k.modifiers.control && !k.modifiers.alt && !k.modifiers.platform)
            k.keyChar = (k.modifiers.shift && c >= u'a' && c <= u'z') ? std::u16string(1, char16_t(c - u'a' + u'A'))
                                                                     : std::u16string(1, c);
    } else if (k.key == u"space" && !k.modifiers.control && !k.modifiers.alt && !k.modifiers.platform) {
        k.keyChar = u" ";
    }
    return k;
}

std::vector<Keystroke> Keystroke::parseSequence(std::u16string_view text)
{
    std::vector<Keystroke> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == u' ')
            ++i;
        std::size_t j = i;
        while (j < text.size() && text[j] != u' ')
            ++j;
        if (j > i) {
            if (auto k = parse(text.substr(i, j - i)))
                out.push_back(*k);
            else
                return {};
        }
        i = j;
    }
    return out;
}

std::u16string Keystroke::toString() const
{
    std::u16string s;
    if (modifiers.control)
        s += u"ctrl-";
    if (modifiers.alt)
        s += u"alt-";
    if (modifiers.shift)
        s += u"shift-";
    if (modifiers.platform)
        s += u"super-";
    if (modifiers.function)
        s += u"fn-";
    s += key;
    return s;
}

Point positionOf(const PlatformInput &input)
{
    return std::visit(
            [](const auto &e) -> Point {
                if constexpr (requires { e.position; })
                    return e.position;
                else
                    return {};
            },
            input);
}

bool isMouseEvent(const PlatformInput &input)
{
    return std::holds_alternative<MouseDownEvent>(input) || std::holds_alternative<MouseUpEvent>(input)
            || std::holds_alternative<MouseMoveEvent>(input) || std::holds_alternative<MouseExitEvent>(input)
            || std::holds_alternative<ScrollWheelEvent>(input);
}

bool isKeyEvent(const PlatformInput &input)
{
    return std::holds_alternative<KeyDownEvent>(input) || std::holds_alternative<KeyUpEvent>(input)
            || std::holds_alternative<ModifiersChangedEvent>(input);
}

} // namespace brulion::atrament
