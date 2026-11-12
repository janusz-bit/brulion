// The bundled text fonts of the "Nocny" look (docs/UI.md "Look & feel"; assets/fonts/ui, made by
// scripts/ui-fonts.py): "Brulion Inter" for the interface and note text (the default sans editor
// font), "Brulion Literata" for the serif editor font and "Brulion JetBrains Mono" for code, dates
// and meta text — subsets of Inter, Literata and JetBrains Mono (SIL OFL 1.1), renamed so a
// complete font of the system with the same name is never mixed up with them. Loaded once at
// start-up, on every platform (the browser's Qt has only DejaVu; Android's system fonts differ by
// maker). Theme.qml uses them as the defaults through DeviceInfo (defaultUiFamily, …); a family
// chosen in the settings wins, and a font that failed to load gives the system's.
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::ui {

inline constexpr char16_t kBundledUiFamily[] = u"Brulion Inter";
inline constexpr char16_t kBundledSerifFamily[] = u"Brulion Literata";
inline constexpr char16_t kBundledMonoFamily[] = u"Brulion JetBrains Mono";

/// The font files (":/fonts/ui/…") of the three families.
QStringList bundledFontFiles();

/// Loads the bundled fonts as application fonts (once per process; later calls return the first
/// call's result): the families that loaded, each once. Needs a QGuiApplication.
QStringList installBundledFonts();

} // namespace brulion::ui
