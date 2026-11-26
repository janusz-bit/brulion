#pragma once

// The gallery (docs/ATRAMENT.md M0): every Atrament element in Brulion's graphite look, for
// developing the framework and for comparing renderers. `atrament-gallery` shows it in a Qt Quick
// window; `atrament-gallery --screenshot out.png` renders one frame with the CPU renderer.

#include "ui/atrament.h"

#include <memory>
#include <string>
#include <vector>

namespace brulion::atrament::gallery {

struct Theme {
    Color background = rgb(0x151618);
    Color surface = rgb(0x1c1d20);
    Color raised = rgb(0x24262a);
    Color hover = rgb(0x2c2e33);
    Color border = rgb(0x2f3136);
    Color text = rgb(0xe9e9ec);
    Color muted = rgb(0x9a9ca4);
    Color faint = rgb(0x6b6e76);
    Color accent = rgb(0x8b93ff);
    Color accentHover = rgb(0xa1a7ff);
    Color accentText = rgb(0x101114);
    Color danger = rgb(0xf0716b);
    Color success = rgb(0x5fd08a);
    Color warning = rgb(0xf2c14e);
};

const Theme &theme();

class Gallery {
public:
    explicit Gallery(Context<Gallery> &cx);
    AnyElement render(Window &window, Context<Gallery> &cx);

    int section = 0;
    int counter = 0;
    bool liked = false;
    std::size_t selectedRow = 3;
    float columnWidth = 360;

private:
    AnyElement sidebar(Window &window, Context<Gallery> &cx);
    AnyElement typography(Window &window, Context<Gallery> &cx);
    AnyElement controls(Window &window, Context<Gallery> &cx);
    AnyElement longList(Window &window, Context<Gallery> &cx);
    AnyElement drawing(Window &window, Context<Gallery> &cx);
    AnyElement footer(Window &window, Context<Gallery> &cx);

    FocusHandle m_focus;
    Entity<TextInput> m_search;
    Entity<TextInput> m_note;
};

// Bundled fonts from the gallery's resources into a collection, with the fallback chain set.
std::shared_ptr<FontCollection> loadFonts();

} // namespace brulion::atrament::gallery
