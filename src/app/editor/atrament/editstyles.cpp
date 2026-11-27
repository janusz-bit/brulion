#include "editstyles.h"

#include "renderedspans.h"

#include <QColor>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

constexpr int ColorRoles = int(ColorRole::String) + 1;
constexpr int BackgroundRoles = int(BackgroundRole::Tint) + 1;

std::optional<at::Color> toColor(const QVariant &v)
{
    QColor c;
    if (!v.isValid())
        return std::nullopt;
    if (v.metaType() == QMetaType::fromType<QColor>())
        c = v.value<QColor>();
    else
        c = QColor::fromString(v.toString());
    if (!c.isValid())
        return std::nullopt;
    return at::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}

// MarkdownHighlighter::paletteColor(): the key, the fallback key, the built-in colour.
std::optional<at::Color> resolve(const QVariantMap &palette, const PaletteColor &source)
{
    if (source.key.isEmpty())
        return std::nullopt;
    if (auto c = toColor(palette.value(QString(source.key))))
        return c;
    if (!source.fallbackKey.isEmpty())
        if (auto c = toColor(palette.value(QString(source.fallbackKey))))
            return c;
    return toColor(QString(source.fallback));
}

} // namespace

static_assert(ColorRoles <= 32 && BackgroundRoles <= 8, "EditStyler's tables");

EditStyler::EditStyler(const QVariantMap &palette, std::u16string codeFamily, float boxRadius)
    : m_codeFamily(std::move(codeFamily))
    , m_boxRadius(boxRadius)
{
    for (int i = 1; i < ColorRoles; ++i)
        m_colors[std::size_t(i)] = resolve(palette, colorRolePalette(ColorRole(i)));
    for (int i = 1; i < BackgroundRoles; ++i)
        m_backgrounds[std::size_t(i)] = resolve(palette, backgroundRolePalette(BackgroundRole(i)));
    // (MarkdownHighlighter::refreshChips' keys and defaults.)
    auto colour = [&palette](const char *key, const char *fallbackKey, const char *fallback) {
        return resolve(palette, PaletteColor{QLatin1StringView(key), QLatin1StringView(fallbackKey),
                                             QLatin1StringView(fallback)})
                .value_or(at::Color{});
    };
    m_reminderBackground = colour("reminderBackground", "", "#38f2b33d");
    m_alarmBackground = colour("alarmBackground", "", "#f2b33d");
    m_reminderText = colour("reminderChip", "reminder", "#7a4f00");
    m_alarmText = colour("alarmChip", "alarm", "#7a4f00");
}

std::vector<at::TextSpan> EditStyler::spans(std::u16string_view text, MarkdownMode mode, bool luau,
                                            Extras *extras) const
{
    std::vector<at::TextSpan> out;
    StyleOptions options;
    options.mode = mode;
    options.luau = luau;
    if (extras) {
        extras->prose.clear();
        extras->chipsFound.clear();
        options.reminderChips = extras->chips && mode == MarkdownMode::Inline;
        options.chipLocale = extras->chipLocale;
        options.chipToday = extras->chipToday;
    }
    LineStyles styles;
    int state = 0;
    std::size_t start = 0;
    for (int index = 0; start <= text.size(); ++index) {
        std::size_t end = text.find(u'\n', start);
        if (end == std::u16string_view::npos)
            end = text.size();
        const QString line(reinterpret_cast<const QChar *>(text.data() + start), qsizetype(end - start));
        options.firstLine = index == 0;
        styleMarkdownLine(line, state, options, &styles);
        state = styles.state;
        if (extras) {
            extras->prose.push_back({int(start), int(line.size()), styles.spell});
            for (LineChip chip : styles.chips) {
                chip.from += int(start);
                chip.to += int(start);
                extras->chipsFound.push_back(std::move(chip));
            }
        }
        for (const StyleRun &run : flattenStyles(styles.ops, int(line.size()))) {
            const StyleAttributes &a = run.attributes;
            at::TextStyleRefinement r;
            if (a.color != ColorRole::None) {
                if (const auto &c = m_colors[std::size_t(a.color)])
                    r.color = *c;
            }
            std::optional<at::Color> back;
            if (a.background == BackgroundRole::Tint)
                back = toColor(a.tintColor());
            else if (a.background != BackgroundRole::None)
                back = m_backgrounds[std::size_t(a.background)];
            if (back) {
                // A box around the glyphs (inline code, a highlight), as the rendered text's.
                at::TextBackground box;
                box.color = *back;
                box.radius = m_boxRadius;
                r.background = std::optional<at::TextBackground>(box);
            }
            if (a.weight != FontWeight::Normal)
                r.fontWeight = at::FontWeight{a.weight == FontWeight::Bold ? at::FontWeight::Bold : at::FontWeight::SemiBold};
            if (a.italic)
                r.fontStyle = at::FontStyle::Italic;
            if (a.strike)
                r.strikethrough = true;
            if (a.underline)
                r.underline = std::optional<at::UnderlineStyle>(at::UnderlineStyle{});
            if (a.monospace && !m_codeFamily.empty())
                r.fontFamily = m_codeFamily;
            if (r.isEmpty())
                continue;
            const int from = int(start) + run.from;
            out.push_back({at::Range{from, from + run.length}, std::move(r)});
        }
        if (end == text.size())
            break;
        start = end + 1;
    }
    return out;
}

at::TextInput::Styler EditStyler::styler(MarkdownMode mode, bool luau) const
{
    auto self = std::make_shared<const EditStyler>(*this);
    return [self, mode, luau](std::u16string_view text) { return self->spans(text, mode, luau); };
}

} // namespace brulion::editor::atr
