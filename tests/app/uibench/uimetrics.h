// UI metrics — the objective half of the UI benchmark (docs/UI.md "UI benchmark (Brulion vs
// Obsidian)"): scores computed from a screenshot (PNG) and its layout dump (schema 1, written by
// Brulion's `--dump-layout`, src/app/ui/layoutdump.h, and by the Obsidian driver
// tests/app/uibench/obsidian-shots.mjs from the DOM).
//
//   C  text contrast (WCAG 1.4.3 on the sampled background)      weight 0.25
//   T  target size (desktop WCAG 2.5.8, phone 44 px)               0.20
//   Y  type scale (few, distinct sizes and weights)                0.15
//   A  alignment (shared left edges, centred rows)                 0.15
//   S  spacing grid (insets and gaps on a 4-px grid)               0.10
//   K  token contrast (theme text colours on its surfaces)         0.10
//   L  line length (45–90 characters per line; desktop reading screens)  0.05
// Every score is 0–10; O is their weighted mean (weights renormalised when L or K is n/a).
// Contrast uses brulion::ui::contrast()/over() of ThemePalette (WCAG 2).
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <optional>

namespace brulion::uibench {

enum class Device { Desktop, Phone };

struct Fragment {
    QString text;
    int chars = 0;
    QColor color;
    std::optional<QColor> background;
};

struct TextItem {
    QRectF rect;
    QString type, name, text, family;
    int chars = 0;
    QColor color;
    double alpha = 1;
    double px = 0;
    int weight = 400;
    int lines = 1;
    bool enabled = true, occluded = false, placeholder = false;
    QList<Fragment> fragments;
};

struct Target {
    QRectF rect;
    QString type, name, kind, label;
    bool enabled = true, inlineTarget = false, occluded = false;
};

struct Box {
    QRectF rect;
    QColor color;
    double alpha = 1;
    double border = 0, radius = 0;
    int paint = 0;
};

struct LayoutDump {
    QString app;
    QSizeF viewport;
    double dpr = 1;
    QList<TextItem> texts;
    QList<Target> targets;
    QList<Box> boxes;
    QHash<QString, QColor> tokens;
};

/// A schema-1 dump; nullopt with `error` set for anything malformed (colours only "#rrggbb" or
/// "#rrggbbaa", rectangles [x, y, w, h] of finite numbers with w, h ≥ 0).
std::optional<LayoutDump> parseDump(const QByteArray &json, QString *error);
/// "#rrggbb" / "#rrggbbaa" (CSS order), else an invalid colour.
QColor parseHex(const QString &text);
/// "#rrggbbaa".
QString toHex(const QColor &c);

/// The background under `logical` (window px) in `png` (device px = logical × dpr): the most
/// frequent colour over the rectangle and a 2-px ring (5-bit bins per channel, then the most
/// frequent exact colour in the winning bin), at most 20 000 pixels sampled (strided).
QColor sampleBackground(const QImage &png, const QRectF &logical, double dpr);

struct TextContrast {
    QString text, type, name;
    int chars = 0;
    double px = 0;
    double ratio = 0, required = 4.5;
    bool large = false, pass = false;
    QColor fg, bg;
};
/// The WCAG contrast of every enabled, non-occluded text with a letter or digit (placeholders
/// count): the minimum over its fragments of ≥ 3 characters.
QList<TextContrast> textContrast(const LayoutDump &dump, const QImage &png);

struct TargetCheck {
    QString type, label, kind;
    QRectF rect;
    bool pass = false;
    bool material = false; ///< phone: ≥ 48 px both sides (Material), reported beside it
    QString reason;
};
/// Every enabled, non-inline, non-occluded target: desktop WCAG 2.5.8 (24 × 24, or a 24-px
/// circle on its centre clear of other targets and other undersized targets' circles — targets
/// containing it do not count), phone ≥ 44 × 44.
QList<TargetCheck> targetSizes(const LayoutDump &dump, Device device);

/// Share (0–1) of the distances on the grid: the left and top insets of texts and targets from
/// the smallest box containing them, the gaps between consecutive texts sharing a left edge
/// (±2 px, 0 < gap < 64). On-grid: within `tolerance` of a multiple of `grid`. 1 without any.
double spacingGridShare(const LayoutDump &dump, int grid = 4, double tolerance = 1.0, QList<double> *distances = nullptr);

struct TypeScale {
    QList<double> sizes; ///< distinct sizes (0.5 px) with ≥ 2 % of the characters, ascending
    QList<int> weights;  ///< distinct weights with ≥ 2 % of the characters
    int nearDuplicates = 0;
    double score = 0; ///< Y, 0–10
};
TypeScale typeScale(const LayoutDump &dump);
/// Y from its inputs: clamp(10 − max(0, n − 6) − max(0, 3 − n) − 1.5·d − max(0, w − 3), 0, 10).
double typeScaleScore(int sizes, int nearDuplicates, int weights);

/// A, 0–10: 10 × (0.6 × share of items whose left edge is shared by ≥ 3 items (1.5 px) + 0.4 ×
/// share of rows of targets and single-line texts whose centres line up within 1.5 px).
double alignmentScore(const LayoutDump &dump);

/// Share (0–1) of the body text's characters in lines of 45–90 characters (body: the most common
/// size ±1 px, ≥ 120 characters, ≥ 2 lines); nullopt on phones or without body text.
std::optional<double> lineLengthShare(const LayoutDump &dump, Device device);

/// K, 0–10: fg, fgMuted, fgFaint on bg and bgSunken, 10 × mean min(1, ratio / 4.5); nullopt
/// without the tokens.
std::optional<double> tokenContrast(const LayoutDump &dump);

/// The screens where line length counts (desktop reading screens).
bool lineLengthScreen(const QString &screen);

struct Objective {
    double C = 0, T = 0, S = 0, Y = 0, A = 0;
    std::optional<double> L, K;
    double score = 0; ///< O
};
/// Every score of one shot. L only for lineLengthScreen() screens on the desktop.
Objective objective(const LayoutDump &dump, const QImage &png, Device device, const QString &screen);
/// O from the parts (weights renormalised over the available ones).
double objectiveScore(const Objective &o);

/// Report-only extras.
double edgeDensity(const QImage &png);
int distinctColours(const QImage &png);
/// Share (0–1) of targets whose dominant ink reaches 3:1 against their surroundings.
double nonTextContrast(const LayoutDump &dump, const QImage &png);

/// The measurement of one shot as JSON: the scores, their details and the failing items (top 30
/// each) for the critic. `meta` (app, screen, size, theme, files) is copied in.
QJsonObject measureJson(const LayoutDump &dump, const QImage &png, Device device, const QString &screen,
                        const QJsonObject &meta = {});

/// metrics.json from the measure outputs: per app/screen/size/theme the scores, per app the
/// means (all shots, and only the pairs both apps have).
QJsonObject summarize(const QList<QJsonObject> &measurements);

/// Errors of screens.json (empty when it is valid): known verbs and arities, unique ids, known
/// sizes, Brulion arguments a list of strings.
QStringList checkManifest(const QByteArray &json);

struct LintFailure {
    QString kind; ///< "contrast" | "target"
    QString type, text, name;
    QString detail;
};
/// The accessibility failures of one shot that `allow` (JSON array of {screen, type, text|name,
/// reason}) does not excuse.
QList<LintFailure> lint(const LayoutDump &dump, const QImage &png, Device device, const QString &screen,
                        const QByteArray &allow, QString *error);

} // namespace brulion::uibench
