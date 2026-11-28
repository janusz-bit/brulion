#include "extensions/appapi.h"

#include "markdown/inline.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

QList<ThemeToken> makeTokens()
{
    QList<ThemeToken> tokens;
    const auto colors = {// surfaces
                         "bg", "bgElevated", "bgElevated2", "bgSunken", "bgHover", "bgPressed", "bgSelected",
                         "bgInverse", "scrim", "scrimModal",
                         // text
                         "fg", "fgMuted", "fgFaint", "fgInverse", "fgOnAccent",
                         // lines
                         "border", "borderSubtle", "borderStrong", "borderControl", "guide", "guideOnSunken",
                         // actions, accent, links, tags
                         "primary", "primaryHover", "primaryPressed", "primaryFg", "accent", "accentHover",
                         "accentText", "accentBg", "accentSolid", "link", "linkHover", "linkMissing", "tagBg", "tagFg",
                         // states
                         "danger", "dangerHover", "dangerFg", "dangerText", "dangerBg", "warning", "warningText",
                         "warningBg", "success", "successText", "successBg", "info", "infoText", "infoBg",
                         // chips
                         "chipDueBg", "chipDueFg", "chipOverdueBg", "chipOverdueFg", "chipScheduledBg",
                         "chipScheduledFg", "chipReminderBg", "chipReminderFg", "chipAlarmBg", "chipAlarmFg",
                         "chipRecurrenceBg", "chipRecurrenceFg", "chipDoneBg", "chipDoneFg", "chipNeutralBg",
                         "chipNeutralFg",
                         // selection, marks, diffs, the rest
                         "selection", "selectionFg", "mark", "focusRing", "diffAddBg", "diffAddStrong", "diffAddFg",
                         "diffDelBg", "diffDelStrong", "diffDelFg", "scrollbar", "switchThumb", "shadow"};
    for (const char *name : colors)
        tokens.append({QString::fromLatin1(name), ThemeTokenKind::Color, 0, 0});
    const struct {
        const char *name;
        double min, max;
    } numbers[] = {{"radiusXs", 0, 24}, {"radiusSm", 0, 24}, {"radius", 0, 32},
                   {"radiusLg", 0, 40}, {"radiusXl", 0, 48}, {"focusRingWidth", 1, 6},
                   // the spacing unit: Theme.space1 (4 px), space2 … space16 its multiples
                   {"space", 3, 6}};
    for (const auto &n : numbers)
        tokens.append({QString::fromLatin1(n.name), ThemeTokenKind::Number, n.min, n.max});
    for (const char *name : {"fontUi", "fontSans", "fontSerif", "fontMono"})
        tokens.append({QString::fromLatin1(name), ThemeTokenKind::Font, 0, 0});
    return tokens;
}

// Edit distance of two short names (the nearest token in an error).
int distance(QStringView a, QStringView b)
{
    QList<int> previous(b.size() + 1), current(b.size() + 1);
    for (qsizetype j = 0; j <= b.size(); ++j)
        previous[j] = int(j);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1].toLower() == b[j - 1].toLower() ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

QString nearestToken(QStringView name)
{
    QString best;
    int bestDistance = 4; // farther than 3 edits suggests nothing
    for (const ThemeToken &t : themeTokens()) {
        const int d = distance(name, t.name);
        if (d < bestDistance) {
            bestDistance = d;
            best = t.name;
        }
    }
    return best;
}

bool tokenValue(const ThemeToken &token, const QVariant &value, QVariant *out, QString *error)
{
    switch (token.kind) {
    case ThemeTokenKind::Color: {
        const QString color =
                value.metaType().id() == QMetaType::QString ? normalizedThemeColor(value.toString()) : QString();
        if (color.isEmpty()) {
            *error = u"“%1” is a colour: \"#rgb\", \"#rrggbb\" or \"#aarrggbb\", not %2"_s.arg(
                    token.name, value.metaType().id() == QMetaType::QString ? u"\""_s + value.toString().left(40) + u'"'
                                                                            : QString::fromLatin1(value.typeName()));
            return false;
        }
        *out = color;
        return true;
    }
    case ThemeTokenKind::Number: {
        bool ok = false;
        const double d = value.metaType().id() == QMetaType::QString ? 0 : value.toDouble(&ok);
        if (!ok || !std::isfinite(d) || d < token.min || d > token.max) {
            *error = u"“%1” is a number from %2 to %3"_s.arg(token.name).arg(token.min).arg(token.max);
            return false;
        }
        *out = d;
        return true;
    }
    case ThemeTokenKind::Font: {
        if (value.metaType().id() != QMetaType::QString || value.toString().size() > 100
            || value.toString().contains(u'\n')) {
            *error = u"“%1” is a font family's name (one line, at most 100 characters)"_s.arg(token.name);
            return false;
        }
        *out = value.toString().trimmed();
        return true;
    }
    }
    return false;
}

bool tokensFrom(const QVariantMap &table, bool allowModes, ThemeLayer *layer, QVariantMap *into, QString *error)
{
    for (auto it = table.cbegin(); it != table.cend(); ++it) {
        const QString &name = it.key();
        if (allowModes && (name == u"dark" || name == u"light")) {
            if (it.value().metaType().id() != QMetaType::QVariantMap) {
                if (it.value().metaType().id() == QMetaType::QVariantList && it.value().toList().isEmpty())
                    continue; // {} arrives as an empty list
                *error = u"“%1” is a table of tokens for the %1 mode"_s.arg(name);
                return false;
            }
            if (!tokensFrom(it.value().toMap(), false, layer, name == u"dark" ? &layer->dark : &layer->light, error))
                return false;
            continue;
        }
        const ThemeToken *token = themeToken(name);
        if (!token) {
            const QString near = nearestToken(name);
            *error = near.isEmpty() ? u"unknown token “%1”"_s.arg(name)
                                    : u"unknown token “%1” (did you mean “%2”?)"_s.arg(name, near);
            if (!allowModes && (name == u"dark" || name == u"light"))
                *error = u"“%1” inside a mode's table"_s.arg(name);
            return false;
        }
        QVariant value;
        if (!tokenValue(*token, it.value(), &value, error))
            return false;
        into->insert(name, value);
    }
    return true;
}

const QRegularExpression &idPattern()
{
    static const QRegularExpression re(u"^[A-Za-z0-9_-]{1,64}$"_s);
    return re;
}

const QRegularExpression &iconPattern()
{
    static const QRegularExpression re(u"^[a-z0-9-]{1,40}$"_s);
    return re;
}

bool textField(const QVariantMap &item, const QString &key, int max, bool oneLine, QString *out, QString *error)
{
    const QVariant v = item.value(key);
    if (!v.isValid() || v.isNull()) {
        out->clear();
        return true;
    }
    if (v.metaType().id() != QMetaType::QString) {
        *error = u"“%1” is a text"_s.arg(key);
        return false;
    }
    QString text = v.toString();
    if (oneLine)
        text = text.simplified();
    if (text.size() > max) {
        *error = u"“%1” has at most %2 characters"_s.arg(key).arg(max);
        return false;
    }
    *out = text;
    return true;
}

} // namespace

bool themeTokenValue(const ThemeToken &token, const QVariant &value, QVariant *out, QString *error)
{
    return tokenValue(token, value, out, error);
}

QString nearestThemeToken(QStringView name)
{
    return nearestToken(name);
}

const QList<ThemeToken> &themeTokens()
{
    static const QList<ThemeToken> tokens = makeTokens();
    return tokens;
}

const ThemeToken *themeToken(QStringView name)
{
    for (const ThemeToken &t : themeTokens())
        if (t.name == name)
            return &t;
    return nullptr;
}

QVariantMap ThemeLayer::forMode(bool darkMode) const
{
    QVariantMap out = both;
    const QVariantMap &mode = darkMode ? dark : light;
    for (auto it = mode.cbegin(); it != mode.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QString normalizedThemeColor(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (!t.startsWith(u'#'))
        return {};
    for (qsizetype i = 1; i < t.size(); ++i)
        if (!t[i].isDigit() && !(t[i] >= u'a' && t[i] <= u'f'))
            return {};
    if (t.size() == 4) // #rgb
        return u"#"_s + t[1] + t[1] + t[2] + t[2] + t[3] + t[3];
    if (t.size() == 7 || t.size() == 9)
        return t;
    return {};
}

bool themeLayerFrom(const QVariantMap &table, ThemeLayer *layer, QString *error)
{
    ThemeLayer out;
    QString why;
    if (!tokensFrom(table, true, &out, &out.both, &why)) {
        *error = why;
        return false;
    }
    if (out.both.size() + out.light.size() + out.dark.size() > MaxThemeTokens) {
        *error = u"at most %1 tokens"_s.arg(MaxThemeTokens);
        return false;
    }
    *layer = out;
    return true;
}

QVariantMap mergedTheme(const QList<ThemeLayer> &layers, bool darkMode)
{
    QVariantMap out;
    for (const ThemeLayer &layer : layers) {
        const QVariantMap tokens = layer.forMode(darkMode);
        for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
}

QVariantMap themeLayerTable(const ThemeLayer &layer)
{
    QVariantMap out = layer.both;
    if (!layer.dark.isEmpty())
        out.insert(u"dark"_s, layer.dark);
    if (!layer.light.isEmpty())
        out.insert(u"light"_s, layer.light);
    return out;
}

QStringList contributionSlots()
{
    return {u"toolbar"_s, u"statusbar"_s,  u"sidebar"_s,   u"pageMenu"_s,        u"blockMenu"_s, u"slash"_s,
            u"tabMenu"_s, u"folderMenu"_s, u"bottomNav"_s, u"keyboardToolbar"_s, u"share"_s};
}

ContributionLimits contributionLimits(QStringView slot)
{
    if (slot == u"bottomNav")
        return {MaxBottomNavItems, MaxBottomNavItems};
    if (slot == u"keyboardToolbar")
        return {MaxKeyboardToolbarPerExtension, MaxKeyboardToolbarItems};
    if (slot == u"share")
        return {MaxShareActionsPerExtension, MaxShareActions};
    return {};
}

bool isContributionSlot(QStringView slot)
{
    for (const QString &s : contributionSlots())
        if (s == slot)
            return true;
    return false;
}

bool contributionFrom(const QString &slot, const QVariantMap &item, Contribution *out, QString *error)
{
    if (!isContributionSlot(slot)) {
        *error = u"unknown slot “%1” (%2)"_s.arg(slot, contributionSlots().join(u", "_s));
        return false;
    }
    static const QSet<QString> known = {u"id"_s,      u"label"_s, u"icon"_s,   u"tooltip"_s, u"tone"_s, u"order"_s,
                                        u"command"_s, u"view"_s,  u"params"_s, u"page"_s,    u"run"_s,  u"insert"_s};
    for (auto it = item.cbegin(); it != item.cend(); ++it) {
        if (!known.contains(it.key())) {
            *error = u"an item has no field “%1” (id, label, icon, tooltip, tone, order, command, view, params, "
                     "page, run, insert)"_s.arg(it.key());
            return false;
        }
    }
    Contribution c;
    c.slot = slot;
    const QVariant id = item.value(u"id"_s);
    if (id.metaType().id() != QMetaType::QString || !idPattern().match(id.toString()).hasMatch()) {
        *error = u"an item needs an “id”: letters, digits, - and _ (at most 64)"_s;
        return false;
    }
    c.id = id.toString();
    if (!textField(item, u"label"_s, MaxContributionLabel, true, &c.label, error)
        || !textField(item, u"tooltip"_s, MaxContributionTooltip, true, &c.tooltip, error)
        || !textField(item, u"icon"_s, 40, true, &c.icon, error)
        || !textField(item, u"tone"_s, 20, true, &c.tone, error)
        || !textField(item, u"command"_s, 64, true, &c.command, error)
        || !textField(item, u"view"_s, 64, true, &c.view, error)
        || !textField(item, u"page"_s, 400, true, &c.page, error)
        || !textField(item, u"run"_s, 200, true, &c.run, error)
        || !textField(item, u"insert"_s, MaxContributionInsert, false, &c.insert, error))
        return false;
    if (!c.icon.isEmpty() && !iconPattern().match(c.icon).hasMatch()) {
        *error = u"“icon” names one of the app's icons (\"dumbbell\", \"calendar-check\")"_s;
        return false;
    }
    static const QStringList tones = {u""_s, u"accent"_s, u"success"_s, u"danger"_s, u"warning"_s, u"info"_s};
    if (!tones.contains(c.tone)) {
        *error = u"“tone” is one of accent, success, danger, warning, info"_s;
        return false;
    }
    if (item.contains(u"params"_s)) {
        const QVariant p = item.value(u"params"_s);
        if (p.metaType().id() == QMetaType::QVariantMap)
            c.params = p.toMap();
        else if (!(p.metaType().id() == QMetaType::QVariantList && p.toList().isEmpty()) && p.isValid())
            return (*error = u"“params” is a table of the view's params"_s, false);
        if (c.view.isEmpty())
            return (*error = u"“params” go with a “view”"_s, false);
    }
    if (item.contains(u"order"_s)) {
        bool ok = false;
        const double order =
                item.value(u"order"_s).metaType().id() == QMetaType::QString ? 0 : item.value(u"order"_s).toDouble(&ok);
        if (!ok || !std::isfinite(order))
            return (*error = u"“order” is a number"_s, false);
        c.order = order;
    }
    const int actions = int(!c.command.isEmpty()) + int(!c.view.isEmpty()) + int(!c.page.isEmpty())
                        + int(!c.run.isEmpty()) + int(!c.insert.isEmpty());
    if (actions > 1) {
        *error = u"an item does one thing: one of command, view, page, run, insert"_s;
        return false;
    }
    if (!c.insert.isEmpty() && slot != u"slash") {
        *error = u"“insert” is for the slash menu only"_s;
        return false;
    }
    if (actions == 0 && slot != u"statusbar") {
        *error = u"an item of “%1” needs what it does: command, view, page or run%2"_s.arg(
                slot, slot == u"slash" ? u", or insert"_s : QString());
        return false;
    }
    if (slot == u"slash" && !c.view.isEmpty()) {
        *error = u"the slash menu runs a command or inserts Markdown, not a view"_s;
        return false;
    }
    if (slot == u"share" && c.command.isEmpty()) {
        *error = u"a share action runs one of the extension's commands (it gets ctx.shared): a “command”"_s;
        return false;
    }
    if ((slot == u"bottomNav" || slot == u"keyboardToolbar") && c.icon.isEmpty()) {
        *error = u"a button of “%1” shows only its icon: it needs an “icon”"_s.arg(slot);
        return false;
    }
    if (c.label.isEmpty()) {
        *error = slot == u"toolbar" || slot == u"bottomNav" || slot == u"keyboardToolbar"
                         ? u"a button needs a “label”: its name and tooltip"_s
                         : u"an item needs a “label”"_s;
        return false;
    }
    *out = c;
    return true;
}

QVariantMap contributionTable(const Contribution &item)
{
    QVariantMap out{
            {u"extension"_s, item.extension}, {u"slot"_s, item.slot}, {u"id"_s, item.id}, {u"label"_s, item.label}};
    const auto put = [&out](const QString &key, const QString &value) {
        if (!value.isEmpty())
            out.insert(key, value);
    };
    put(u"icon"_s, item.icon);
    put(u"tooltip"_s, item.tooltip);
    put(u"tone"_s, item.tone);
    put(u"command"_s, item.command);
    put(u"view"_s, item.view);
    put(u"page"_s, item.page);
    put(u"run"_s, item.run);
    put(u"insert"_s, item.insert);
    if (!item.params.isEmpty())
        out.insert(u"params"_s, item.params);
    if (item.order != 0)
        out.insert(u"order"_s, item.order);
    return out;
}

namespace {

/// The fields of `table` are among `known` (listed in the error as `names`).
bool knownFields(const QVariantMap &table, const QStringList &known, const QString &what, QString *error)
{
    for (auto it = table.cbegin(); it != table.cend(); ++it) {
        if (!known.contains(it.key())) {
            *error = u"%1 has no field “%2” (%3)"_s.arg(what, it.key(), known.join(u", "_s));
            return false;
        }
    }
    return true;
}

/// An optional boolean field: `fallback` when it is missing.
bool boolField(const QVariantMap &table, const QString &key, bool fallback, bool *out, QString *error)
{
    const QVariant v = table.value(key);
    if (!v.isValid() || v.isNull()) {
        *out = fallback;
        return true;
    }
    if (v.metaType().id() != QMetaType::Bool) {
        *error = u"“%1” is true or false"_s.arg(key);
        return false;
    }
    *out = v.toBool();
    return true;
}

/// A tag as written ("#pilne", "#[[Jan Kowalski]]", "a/") as its name: no '#', no `[[…]]`, no
/// trailing '/', NFC.
QString tagName(const QString &tag)
{
    QString name = tag.trimmed();
    if (name.startsWith(u'#'))
        name.remove(0, 1);
    if (name.startsWith(u"[[") && name.endsWith(u"]]") && name.size() >= 4)
        name = name.mid(2, name.size() - 4);
    name = name.trimmed().normalized(QString::NormalizationForm_C);
    while (name.endsWith(u'/'))
        name.chop(1);
    return name;
}

bool hasLineBreak(const QString &text)
{
    for (const QChar c : text)
        if (c == u'\n' || c == u'\r' || c == QChar(0x2028) || c == QChar(0x2029))
            return true;
    return false;
}

} // namespace

bool inputRuleFrom(const QVariantMap &table, InputRule *out, QString *error)
{
    if (!knownFields(table, {u"match"_s, u"replace"_s, u"word"_s}, u"an input rule"_s, error))
        return false;
    InputRule rule;
    const QVariant match = table.value(u"match"_s);
    const QVariant replace = table.value(u"replace"_s);
    if (match.metaType().id() != QMetaType::QString || match.toString().isEmpty()) {
        *error = u"an input rule needs “match”: the text typed (\"->\")"_s;
        return false;
    }
    if (replace.metaType().id() != QMetaType::QString) {
        *error = u"an input rule needs “replace”: the text it becomes (\"→\")"_s;
        return false;
    }
    rule.match = match.toString().normalized(QString::NormalizationForm_C);
    rule.replace = replace.toString().normalized(QString::NormalizationForm_C);
    if (!boolField(table, u"word"_s, false, &rule.word, error))
        return false;
    if (rule.match.size() > MaxInputRuleMatch) {
        *error = u"“match” has at most %1 characters"_s.arg(MaxInputRuleMatch);
        return false;
    }
    if (rule.replace.size() > MaxInputRuleReplace) {
        *error = u"“replace” has at most %1 characters"_s.arg(MaxInputRuleReplace);
        return false;
    }
    if (hasLineBreak(rule.match) || hasLineBreak(rule.replace)) {
        *error = u"an input rule's texts are one line"_s;
        return false;
    }
    if (rule.match == rule.replace) {
        *error = u"“replace” is “match” itself"_s;
        return false;
    }
    if (rule.word && (rule.match.front().isSpace() || rule.match.back().isSpace())) {
        *error = u"a “word” rule's “match” has no spaces around it"_s;
        return false;
    }
    *out = rule;
    return true;
}

QVariantMap inputRuleTable(const InputRule &rule)
{
    return {{u"extension"_s, rule.extension},
            {u"match"_s, rule.match},
            {u"replace"_s, rule.replace},
            {u"word"_s, rule.word}};
}

QStringList tagTints()
{
    return {u"accent"_s, u"success"_s, u"danger"_s, u"warning"_s, u"info"_s, u"neutral"_s};
}

QString tagStyleKey(const QString &tag)
{
    const QString name = tagName(tag);
    if (name.isEmpty() || markdown::tagSyntax(name).isEmpty())
        return {};
    return name.toLower();
}

bool tagStyleFrom(const QVariantMap &table, TagStyle *out, QString *error)
{
    if (!knownFields(table, {u"tag"_s, u"tint"_s, u"fill"_s, u"bar"_s}, u"a tag style"_s, error))
        return false;
    TagStyle style;
    const QVariant tag = table.value(u"tag"_s);
    if (tag.metaType().id() != QMetaType::QString || tag.toString().trimmed().isEmpty()) {
        *error = u"a tag style needs “tag”: the tag's name (\"pilne\")"_s;
        return false;
    }
    if (tag.toString().size() > 200 || tagStyleKey(tag.toString()).isEmpty()) {
        *error = u"“%1” is no tag's name"_s.arg(tag.toString().left(200));
        return false;
    }
    style.tag = tagName(tag.toString());
    const QVariant tint = table.value(u"tint"_s);
    const QString tintText = tint.metaType().id() == QMetaType::QString ? tint.toString().trimmed() : QString();
    if (tagTints().contains(tintText)) {
        style.tint = tintText;
    } else {
        const QString color = normalizedThemeColor(tintText);
        if (color.size() != 7) {
            *error = u"“tint” is one of %1, or a colour \"#rrggbb\""_s.arg(tagTints().join(u", "_s));
            return false;
        }
        style.tint = color;
    }
    if (!boolField(table, u"fill"_s, true, &style.fill, error) || !boolField(table, u"bar"_s, true, &style.bar, error))
        return false;
    if (!style.fill && !style.bar) {
        *error = u"a tag style shows a “fill”, a “bar” or both"_s;
        return false;
    }
    *out = style;
    return true;
}

QVariantMap tagStyleTable(const TagStyle &style)
{
    return {{u"extension"_s, style.extension},
            {u"tag"_s, style.tag},
            {u"tint"_s, style.tint},
            {u"fill"_s, style.fill},
            {u"bar"_s, style.bar}};
}

QStringList replaceableViews()
{
    static const QStringList views{u"journals"_s, u"tasks"_s,    u"calendar"_s, u"habits"_s,
                                   u"graph"_s,    u"allPages"_s, u"tags"_s,     u"search"_s};
    return views;
}

bool viewReplacementFrom(const QString &builtin, const QString &view, const QVariantMap &params, ViewReplacement *out,
                         QString *error)
{
    if (!replaceableViews().contains(builtin)) {
        *error = u"“%1” cannot be replaced (one of %2)"_s.arg(builtin.left(100), replaceableViews().join(u", "_s));
        return false;
    }
    if (view.isEmpty() || view.size() > 100) {
        *error = u"argument 2 is the name of one of the extension's views"_s;
        return false;
    }
    if (QJsonDocument::fromVariant(params).toJson(QJsonDocument::Compact).size() > MaxViewReplacementParams) {
        *error = u"the view's params are at most %1 bytes as JSON"_s.arg(MaxViewReplacementParams);
        return false;
    }
    *out = ViewReplacement{QString(), builtin, view, params};
    return true;
}

QVariantMap replacedRoute(const QVariantMap &route, const ViewReplacement &replacement)
{
    if (route.value(u"view"_s).toString() != replacement.builtin || route.value(u"builtin"_s).toBool())
        return route;
    QVariantMap out = replacement.params;
    for (auto [key, value] : route.asKeyValueRange())
        out.insert(key, value);
    out.insert(u"view"_s, u"extension"_s);
    out.insert(u"extension"_s, replacement.extension);
    out.insert(u"name"_s, replacement.view);
    out.insert(u"replaces"_s, replacement.builtin);
    return out;
}

QVariantMap viewReplacementTable(const ViewReplacement &replacement)
{
    return {{u"extension"_s, replacement.extension},
            {u"builtin"_s, replacement.builtin},
            {u"view"_s, replacement.view},
            {u"params"_s, replacement.params}};
}

namespace {

QList<NotebookSetting> makeNotebookSettings()
{
    const auto spec = [](const char *key, const char *kind, const char *label, QStringList options = {}) {
        return NotebookSetting{QString::fromLatin1(key), QString::fromLatin1(kind), std::move(options),
                               QString::fromUtf8(label)};
    };
    const QStringList days{u"monday"_s, u"tuesday"_s, u"wednesday"_s, u"thursday"_s,
                           u"friday"_s, u"saturday"_s, u"sunday"_s};
    return {
            spec("journalsFolder", "path", "the folder of the daily notes"),
            spec("journalFileFormat", "text", "the daily notes' file names, a date format (\"yyyy-MM-dd\")"),
            spec("journalTemplate", "path", "the daily notes' template, a page's path (\"templates/journal.md\")"),
            spec("templatesFolder", "path", "the folder of the templates"),
            spec("newPagesFolder", "path", "the folder of new pages (\"\": the notebook's root)"),
            spec("attachmentsFolder", "path", "the folder of pasted pictures and files"),
            spec("weeklyFolder", "path", "the folder of the weekly notes"),
            spec("weeklyFileFormat", "text", "the weekly notes' file names (\"yyyy-'W'ww\")"),
            spec("weeklyTemplate", "path", "the weekly notes' template"),
            spec("monthlyFolder", "path", "the folder of the monthly notes"),
            spec("monthlyFileFormat", "text", "the monthly notes' file names (\"yyyy-MM\")"),
            spec("monthlyTemplate", "path", "the monthly notes' template"),
            spec("journalMode", "choice", "how daily notes are edited", {u"outline"_s, u"document"_s}),
            spec("pageMode", "choice", "how new pages are edited", {u"document"_s, u"outline"_s}),
            spec("defaultReminderTime", "text", "the time of a reminder given without one, \"HH:mm\""),
            spec("weekStartsOn", "choice", "the first day of the week", days),
            // The flashcards' card settings (the "flashcards" object; docs/SYNTAX.md §15).
            spec("flashcards.tags", "text", "the tags of the decks, \"flashcards, fiszki\" (\"\": this device's)"),
            spec("flashcards.singleLineSeparator", "text", "a one-line card's separator (\"::\")"),
            spec("flashcards.singleLineReversedSeparator", "text", "a reversed one-line card's separator (\":::\")"),
            spec("flashcards.multilineSeparator", "text", "a multi-line card's separator line (\"?\")"),
            spec("flashcards.multilineReversedSeparator", "text", "a reversed multi-line card's separator (\"??\")"),
            spec("flashcards.multilineEndMarker", "text", "the line that ends a multi-line card (\"\": a blank line)"),
            spec("flashcards.clozeHighlights", "toggle", "==highlights== are clozes"),
            spec("flashcards.clozeBold", "toggle", "**bold text** is a cloze"),
            spec("flashcards.clozeCurly", "toggle", "{{curly brackets}} are clozes"),
            spec("flashcards.foldersAsDecks", "toggle", "a card's deck is its note's folder"),
            spec("flashcards.reviewTags", "text", "the tags of notes reviewed as a whole, \"review\" (\"\": that)"),
            spec("themePage", "path", "the notebook's theme page (\"\": none; Settings → Appearance)"),
    };
}

} // namespace

const QList<NotebookSetting> &notebookSettings()
{
    static const QList<NotebookSetting> settings = makeNotebookSettings();
    return settings;
}

const NotebookSetting *notebookSetting(QStringView key)
{
    for (const NotebookSetting &s : notebookSettings()) {
        if (s.key == key)
            return &s;
    }
    return nullptr;
}

QVariant notebookSettingValue(const NotebookSetting &setting, const QVariantMap &config)
{
    const qsizetype dot = setting.key.indexOf(u'.');
    const QVariant v = dot < 0 ? config.value(setting.key)
                               : config.value(setting.key.left(dot)).toMap().value(setting.key.mid(dot + 1));
    if (setting.key == u"weekStartsOn") {
        const int day = v.toInt();
        return day >= 1 && day <= int(setting.options.size()) ? setting.options.at(day - 1) : setting.options.first();
    }
    return setting.kind == u"toggle" ? QVariant(v.toBool()) : QVariant(v.toString());
}

QVariantMap notebookSettingChange(const NotebookSetting &setting, const QVariant &value)
{
    QVariant v = value;
    if (setting.key == u"weekStartsOn")
        v = int(setting.options.indexOf(value.toString())) + 1;
    const qsizetype dot = setting.key.indexOf(u'.');
    return dot < 0 ? QVariantMap{{setting.key, v}}
                   : QVariantMap{{setting.key.left(dot), QVariantMap{{setting.key.mid(dot + 1), v}}}};
}

QVariantList notebookSettingRows(const QVariantMap &config)
{
    QVariantList rows;
    for (const NotebookSetting &s : notebookSettings()) {
        QVariantMap row{{u"key"_s, s.key},
                        {u"scope"_s, u"notebook"_s},
                        {u"group"_s, u"notebook"_s},
                        {u"kind"_s, s.kind},
                        {u"label"_s, s.label},
                        {u"value"_s, notebookSettingValue(s, config)}};
        if (!s.options.isEmpty())
            row.insert(u"options"_s, s.options);
        rows << row;
    }
    return rows;
}

QStringList appOperations()
{
    return {u"state"_s,    u"commands"_s, u"settings"_s, u"get"_s, u"set"_s,
            u"navigate"_s, u"open"_s,     u"back"_s,     u"run"_s, u"toast"_s};
}

bool isAppChange(QStringView operation)
{
    return operation == u"set" || operation == u"navigate" || operation == u"open" || operation == u"back"
           || operation == u"run" || operation == u"toast";
}

} // namespace brulion::extensions
