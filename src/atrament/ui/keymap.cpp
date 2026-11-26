#include "ui/keymap.h"

namespace brulion::atrament {

bool Keymap::bind(std::u16string_view keystrokes, std::u16string_view action, std::u16string_view context)
{
    std::vector<Keystroke> keys = Keystroke::parseSequence(keystrokes);
    if (keys.empty())
        return false;
    m_bindings.push_back(KeyBinding { std::move(keys), std::u16string(action), std::u16string(context) });
    return true;
}

Keymap::Result Keymap::match(const std::vector<Keystroke> &typed, std::u16string_view context) const
{
    Result result;
    bool pending = false;
    // The newest binding first: user keymaps are loaded after the defaults.
    for (auto it = m_bindings.rbegin(); it != m_bindings.rend(); ++it) {
        const KeyBinding &b = *it;
        if (b.context != context || b.keystrokes.size() < typed.size())
            continue;
        bool prefix = true;
        for (std::size_t i = 0; i < typed.size(); ++i) {
            if (!b.keystrokes[i].matches(typed[i])) {
                prefix = false;
                break;
            }
        }
        if (!prefix)
            continue;
        if (b.keystrokes.size() == typed.size()) {
            if (!result.binding)
                result.binding = &b;
        } else {
            pending = true;
        }
    }
    // A complete binding wins at once over a longer one it is the start of (no waiting for a
    // timeout as GPUI does: such conflicts are rare and the wait makes the key feel slow).
    if (result.binding)
        result.match = Match::Complete;
    else if (pending)
        result.match = Match::Pending;
    return result;
}

std::vector<const KeyBinding *> Keymap::bindingsFor(std::u16string_view action) const
{
    std::vector<const KeyBinding *> out;
    for (auto it = m_bindings.rbegin(); it != m_bindings.rend(); ++it) {
        if (it->action == action)
            out.push_back(&*it);
    }
    return out;
}

} // namespace brulion::atrament
