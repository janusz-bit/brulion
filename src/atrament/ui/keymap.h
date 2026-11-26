#pragma once

// Key bindings (docs/ATRAMENT.md D10): keystroke sequences bound to named actions in a key
// context. A focused element's path from the root carries key contexts ("Workspace", "Editor");
// a binding with context "Editor" applies where an element of that context is on the path, an
// empty context everywhere. Later bindings win over earlier ones (user keymaps load last), deeper
// contexts over shallower ones. "ctrl-k ctrl-s" is a sequence of two keystrokes.

#include "ui/events.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace brulion::atrament {

struct KeyBinding {
    std::vector<Keystroke> keystrokes;
    std::u16string action;
    std::u16string context;
};

class Keymap {
public:
    // Returns false (and binds nothing) for a malformed keystroke string.
    bool bind(std::u16string_view keystrokes, std::u16string_view action, std::u16string_view context = {});
    void clear() { m_bindings.clear(); }
    const std::vector<KeyBinding> &bindings() const { return m_bindings; }

    enum class Match : std::uint8_t { None, Pending, Complete };
    struct Result {
        Match match = Match::None;
        const KeyBinding *binding = nullptr;
    };
    // `typed` (the pending keystrokes and the new one) against the bindings of exactly `context`
    // (empty: global). A complete match wins over a longer binding that could still match.
    Result match(const std::vector<Keystroke> &typed, std::u16string_view context) const;

    // The bindings for an action, for menus and tooltips ("Save  Ctrl+S").
    std::vector<const KeyBinding *> bindingsFor(std::u16string_view action) const;

private:
    std::vector<KeyBinding> m_bindings;
};

} // namespace brulion::atrament
