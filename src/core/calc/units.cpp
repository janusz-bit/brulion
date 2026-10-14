#include "calc/units.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace brulion::calc {

namespace {

constexpr Dimension kNone = {0, 0, 0, 0, 0, 0};
constexpr Dimension kLength = {1, 0, 0, 0, 0, 0};
constexpr Dimension kMass = {0, 1, 0, 0, 0, 0};
constexpr Dimension kTime = {0, 0, 1, 0, 0, 0};
constexpr Dimension kTemperature = {0, 0, 0, 1, 0, 0};
constexpr Dimension kData = {0, 0, 0, 0, 1, 0};
constexpr Dimension kAngle = {0, 0, 0, 0, 0, 1};
constexpr Dimension kArea = {2, 0, 0, 0, 0, 0};
constexpr Dimension kVolume = {3, 0, 0, 0, 0, 0};
constexpr Dimension kSpeed = {1, 0, -1, 0, 0, 0};
constexpr Dimension kForce = {1, 1, -2, 0, 0, 0};
constexpr Dimension kEnergy = {2, 1, -2, 0, 0, 0};
constexpr Dimension kPower = {2, 1, -3, 0, 0, 0};
constexpr Dimension kPressure = {-1, 1, -2, 0, 0, 0};
constexpr Dimension kFrequency = {0, 0, -1, 0, 0, 0};

// Indices into kUnits used by other entries and by the simplifier.
enum UnitIndex {
    Mm, Cm, Dm, M, Km, Um, Inch, Ft, Yd, Mi, Nmi,
    Mg, G, Dag, Kg, Tonne, Oz, Lb,
    Ms, S, Min, H, Day, Week, Month, Year,
    Mm2, Cm2, Dm2, M2, Km2, Ha, Are,
    Mm3, Cm3, Dm3, M3, Ml, Cl, Dl, L, Gal,
    Mph,
    Bit, Byte, KB, MB, GB, TB, KiB, MiB, GiB, TiB, Kbit, Mbit, Gbit,
    J, KJ, MJ, Wh, KWh, MWh, Kcal,
    W, KW, MW, KM, Hp,
    N, KN,
    Pa, HPa, KPa, MPa, Bar,
    Hz, KHz, MHz, GHz,
    Kelvin, Celsius, Fahrenheit,
    Rad, Deg,
    UnitCount
};

constexpr double kDay = 86400;
constexpr double kYear = 365.25 * kDay;

constexpr UnitDef unit(QStringView symbol, const Dimension &dimension, double factor, QStringView symbolPl = {})
{
    UnitDef u;
    u.symbol = symbol;
    u.symbolPl = symbolPl;
    u.dimension = dimension;
    u.factor = factor;
    return u;
}

constexpr UnitDef powerUnit(QStringView symbol, const Dimension &dimension, double factor, int powerOf, int power)
{
    UnitDef u = unit(symbol, dimension, factor);
    u.powerOf = powerOf;
    u.power = power;
    return u;
}

constexpr UnitDef temperatureUnit(QStringView symbol, double factor, double offset)
{
    UnitDef u = unit(symbol, kTemperature, factor);
    u.offset = offset;
    return u;
}

const std::array<UnitDef, UnitCount> kUnits = {{
    unit(u"mm", kLength, 1e-3),
    unit(u"cm", kLength, 1e-2),
    unit(u"dm", kLength, 1e-1),
    unit(u"m", kLength, 1),
    unit(u"km", kLength, 1e3),
    unit(u"µm", kLength, 1e-6),
    unit(u"in", kLength, 0.0254),
    unit(u"ft", kLength, 0.3048),
    unit(u"yd", kLength, 0.9144),
    unit(u"mi", kLength, 1609.344),
    unit(u"nmi", kLength, 1852),

    unit(u"mg", kMass, 1e-6),
    unit(u"g", kMass, 1e-3),
    unit(u"dag", kMass, 1e-2),
    unit(u"kg", kMass, 1),
    unit(u"t", kMass, 1e3),
    unit(u"oz", kMass, 0.028349523125),
    unit(u"lb", kMass, 0.45359237),

    unit(u"ms", kTime, 1e-3),
    unit(u"s", kTime, 1),
    unit(u"min", kTime, 60),
    unit(u"h", kTime, 3600),
    unit(u"d", kTime, kDay),
    unit(u"wk", kTime, 7 * kDay, u"tydz."),
    unit(u"mo", kTime, kYear / 12, u"mies."),
    unit(u"yr", kTime, kYear, u"r."),

    powerUnit(u"mm²", kArea, 1e-6, Mm, 2),
    powerUnit(u"cm²", kArea, 1e-4, Cm, 2),
    powerUnit(u"dm²", kArea, 1e-2, Dm, 2),
    powerUnit(u"m²", kArea, 1, M, 2),
    powerUnit(u"km²", kArea, 1e6, Km, 2),
    unit(u"ha", kArea, 1e4),
    unit(u"a", kArea, 1e2),

    powerUnit(u"mm³", kVolume, 1e-9, Mm, 3),
    powerUnit(u"cm³", kVolume, 1e-6, Cm, 3),
    powerUnit(u"dm³", kVolume, 1e-3, Dm, 3),
    powerUnit(u"m³", kVolume, 1, M, 3),
    unit(u"ml", kVolume, 1e-6),
    unit(u"cl", kVolume, 1e-5),
    unit(u"dl", kVolume, 1e-4),
    unit(u"l", kVolume, 1e-3),
    unit(u"gal", kVolume, 0.003785411784),

    unit(u"mph", kSpeed, 0.44704),

    unit(u"b", kData, 1),
    unit(u"B", kData, 8),
    unit(u"kB", kData, 8e3),
    unit(u"MB", kData, 8e6),
    unit(u"GB", kData, 8e9),
    unit(u"TB", kData, 8e12),
    unit(u"KiB", kData, 8.0 * 1024),
    unit(u"MiB", kData, 8.0 * 1024 * 1024),
    unit(u"GiB", kData, 8.0 * 1024 * 1024 * 1024),
    unit(u"TiB", kData, 8.0 * 1024 * 1024 * 1024 * 1024),
    unit(u"kb", kData, 1e3),
    unit(u"Mb", kData, 1e6),
    unit(u"Gb", kData, 1e9),

    unit(u"J", kEnergy, 1),
    unit(u"kJ", kEnergy, 1e3),
    unit(u"MJ", kEnergy, 1e6),
    unit(u"Wh", kEnergy, 3600),
    unit(u"kWh", kEnergy, 3.6e6),
    unit(u"MWh", kEnergy, 3.6e9),
    unit(u"kcal", kEnergy, 4184),

    unit(u"W", kPower, 1),
    unit(u"kW", kPower, 1e3),
    unit(u"MW", kPower, 1e6),
    unit(u"KM", kPower, 735.49875),
    unit(u"hp", kPower, 745.69987158227022),

    unit(u"N", kForce, 1),
    unit(u"kN", kForce, 1e3),

    unit(u"Pa", kPressure, 1),
    unit(u"hPa", kPressure, 1e2),
    unit(u"kPa", kPressure, 1e3),
    unit(u"MPa", kPressure, 1e6),
    unit(u"bar", kPressure, 1e5),

    unit(u"Hz", kFrequency, 1),
    unit(u"kHz", kFrequency, 1e3),
    unit(u"MHz", kFrequency, 1e6),
    unit(u"GHz", kFrequency, 1e9),

    temperatureUnit(u"K", 1, 0),
    temperatureUnit(u"°C", 1, 273.15),
    temperatureUnit(u"°F", 5.0 / 9.0, 459.67 * 5.0 / 9.0),

    unit(u"rad", kAngle, 1),
    unit(u"°", kAngle, std::numbers::pi / 180),
}};

struct Alias {
    QStringView name;
    int unit;
};

const Alias kAliases[] = {
    {u"mm", Mm}, {u"cm", Cm}, {u"dm", Dm}, {u"m", M}, {u"km", Km},
    {u"µm", Um}, {u"μm", Um}, {u"um", Um},
    {u"in", Inch}, {u"inch", Inch}, {u"inches", Inch}, {u"cal", Inch}, {u"cali", Inch}, {u"cale", Inch},
    {u"ft", Ft}, {u"foot", Ft}, {u"feet", Ft},
    {u"yd", Yd}, {u"yard", Yd}, {u"yards", Yd},
    {u"mi", Mi}, {u"mile", Mi}, {u"miles", Mi}, {u"mila", Mi}, {u"mil", Mi},
    {u"nmi", Nmi},

    {u"mg", Mg}, {u"g", G}, {u"dag", Dag}, {u"dkg", Dag}, {u"kg", Kg}, {u"t", Tonne},
    {u"oz", Oz}, {u"lb", Lb}, {u"lbs", Lb},

    {u"ms", Ms},
    {u"s", S}, {u"sec", S}, {u"sek", S}, {u"second", S}, {u"seconds", S},
    {u"sekunda", S}, {u"sekundy", S}, {u"sekund", S}, {u"sekundę", S},
    {u"min", Min}, {u"minute", Min}, {u"minutes", Min},
    {u"minuta", Min}, {u"minuty", Min}, {u"minut", Min}, {u"minutę", Min},
    {u"h", H}, {u"hr", H}, {u"hour", H}, {u"hours", H},
    {u"godz", H}, {u"godzina", H}, {u"godziny", H}, {u"godzin", H}, {u"godzinę", H},
    {u"d", Day}, {u"day", Day}, {u"days", Day},
    {u"dzień", Day}, {u"dni", Day}, {u"dnia", Day}, {u"doba", Day}, {u"doby", Day}, {u"dób", Day},
    {u"week", Week}, {u"weeks", Week}, {u"wk", Week},
    {u"tydzień", Week}, {u"tygodnie", Week}, {u"tygodni", Week}, {u"tygodnia", Week}, {u"tyg", Week},
    {u"month", Month}, {u"months", Month},
    {u"miesiąc", Month}, {u"miesiące", Month}, {u"miesięcy", Month}, {u"miesiąca", Month}, {u"mies", Month},
    {u"year", Year}, {u"years", Year}, {u"yr", Year},
    {u"rok", Year}, {u"roku", Year}, {u"lata", Year}, {u"lat", Year},

    {u"mm2", Mm2}, {u"cm2", Cm2}, {u"dm2", Dm2}, {u"m2", M2}, {u"km2", Km2}, {u"ha", Ha},
    {u"ar", Are}, {u"ary", Are}, {u"arów", Are},
    {u"mm3", Mm3}, {u"cm3", Cm3}, {u"dm3", Dm3}, {u"m3", M3},
    {u"ml", Ml}, {u"cl", Cl}, {u"dl", Dl}, {u"l", L}, {u"L", L},
    {u"litr", L}, {u"litry", L}, {u"litrów", L}, {u"gal", Gal},

    {u"mph", Mph},

    {u"b", Bit}, {u"bit", Bit}, {u"bits", Bit}, {u"B", Byte}, {u"byte", Byte}, {u"bytes", Byte},
    {u"kB", KB}, {u"KB", KB}, {u"MB", MB}, {u"GB", GB}, {u"TB", TB},
    {u"KiB", KiB}, {u"MiB", MiB}, {u"GiB", GiB}, {u"TiB", TiB},
    {u"kb", Kbit}, {u"Mb", Mbit}, {u"Gb", Gbit},

    {u"J", J}, {u"kJ", KJ}, {u"MJ", MJ}, {u"Wh", Wh}, {u"kWh", KWh}, {u"MWh", MWh}, {u"kcal", Kcal},
    {u"W", W}, {u"kW", KW}, {u"MW", MW}, {u"KM", KM}, {u"hp", Hp},
    {u"N", N}, {u"kN", KN},
    {u"Pa", Pa}, {u"hPa", HPa}, {u"kPa", KPa}, {u"MPa", MPa}, {u"bar", Bar},
    {u"Hz", Hz}, {u"kHz", KHz}, {u"MHz", MHz}, {u"GHz", GHz},

    {u"K", Kelvin},
    {u"C", Celsius}, {u"°C", Celsius}, {u"degC", Celsius},
    {u"F", Fahrenheit}, {u"°F", Fahrenheit}, {u"degF", Fahrenheit},

    {u"rad", Rad}, {u"deg", Deg}, {u"°", Deg},
};

const QHash<QString, const UnitDef *> &unitIndex()
{
    static const QHash<QString, const UnitDef *> index = [] {
        QHash<QString, const UnitDef *> h;
        for (const Alias &a : kAliases)
            h.insert(a.name.toString(), &kUnits[static_cast<std::size_t>(a.unit)]);
        return h;
    }();
    return index;
}

const UnitDef *unitAt(int index)
{
    return &kUnits[static_cast<std::size_t>(index)];
}

/// Unit powers saturate at ±2^20, so even absurd input cannot overflow an int (the evaluator
/// rejects anything beyond m^99 long before).
int saturatedPower(qint64 power)
{
    constexpr qint64 kLimit = qint64(1) << 20;
    return static_cast<int>(std::clamp(power, -kLimit, kLimit));
}

void addDimension(Dimension &acc, const Dimension &d, int power)
{
    for (std::size_t i = 0; i < acc.size(); ++i)
        acc[i] += d[i] * power;
}

/// Replace km², cm³, ... by powers of their base unit so they can merge with plain lengths.
QList<UnitFactor> expandPowers(const QList<UnitFactor> &factors)
{
    QList<UnitFactor> out;
    out.reserve(factors.size());
    for (const UnitFactor &f : factors) {
        if (f.def->powerOf >= 0)
            out.append({unitAt(f.def->powerOf), saturatedPower(qint64(f.def->power) * f.power)});
        else
            out.append(f);
    }
    return out;
}

/// Merge equal units and units of the same dimension into the first occurrence.
QList<UnitFactor> mergeFactors(const QList<UnitFactor> &factors)
{
    QList<UnitFactor> out;
    for (const UnitFactor &f : factors) {
        bool merged = false;
        for (UnitFactor &g : out) {
            if (g.def == f.def || g.def->dimension == f.def->dimension) {
                g.power = saturatedPower(qint64(g.power) + f.power);
                merged = true;
                break;
            }
        }
        if (!merged)
            out.append(f);
    }
    out.removeIf([](const UnitFactor &f) { return f.power == 0; });
    return out;
}

/// {W, h} -> Wh, {kW, h} -> kWh, {MW, h} -> MWh.
std::optional<UnitFactor> compoundAlias(const QList<UnitFactor> &factors)
{
    if (factors.size() != 2 || factors.at(0).power != 1 || factors.at(1).power != 1)
        return std::nullopt;
    struct Rule {
        int power;
        int result;
    };
    static constexpr Rule kRules[] = {{W, Wh}, {KW, KWh}, {MW, MWh}};
    const UnitDef *hour = unitAt(H);
    for (const Rule &r : kRules) {
        const UnitDef *p = unitAt(r.power);
        if ((factors.at(0).def == p && factors.at(1).def == hour) || (factors.at(0).def == hour && factors.at(1).def == p))
            return UnitFactor{unitAt(r.result), 1};
    }
    return std::nullopt;
}

QString factorText(const UnitFactor &f, int power, CalcLocale locale)
{
    QString s = f.def->displaySymbol(locale).toString();
    if (power != 1)
        s += superscript(power);
    return s;
}

} // namespace

bool UnitDef::isTemperature() const
{
    return dimension == kTemperature;
}

const UnitDef *findUnit(QStringView name)
{
    return unitIndex().value(name.toString(), nullptr);
}

const UnitDef *findQuantityUnit(QStringView name)
{
    if (name == u"a")
        return unitAt(Are);
    return findUnit(name);
}

Unit::Unit(const UnitDef *def)
{
    if (def)
        m_factors.append({def, 1});
}

Dimension Unit::dimension() const
{
    Dimension d = kNone;
    for (const UnitFactor &f : m_factors)
        addDimension(d, f.def->dimension, f.power);
    return d;
}

bool Unit::isDimensionless() const
{
    return dimension() == kNone;
}

double Unit::scale() const
{
    double s = 1;
    for (const UnitFactor &f : m_factors)
        s *= std::pow(f.def->factor, f.power);
    return s;
}

const UnitDef *Unit::single() const
{
    return m_factors.size() == 1 && m_factors.at(0).power == 1 ? m_factors.at(0).def : nullptr;
}

QString Unit::toString(CalcLocale locale) const
{
    QStringList numerator;
    QStringList denominator;
    for (const UnitFactor &f : m_factors) {
        if (f.power > 0)
            numerator.append(factorText(f, f.power, locale));
        else if (f.power < 0)
            denominator.append(factorText(f, -f.power, locale));
    }
    const QString dot = QStringLiteral("·");
    if (numerator.isEmpty()) {
        QStringList parts;
        for (const UnitFactor &f : m_factors)
            parts.append(factorText(f, f.power, locale));
        return parts.join(dot);
    }
    QString out = numerator.join(dot);
    if (denominator.size() == 1)
        out += u'/' + denominator.at(0);
    else if (denominator.size() > 1)
        out += QStringLiteral("/(") + denominator.join(dot) + u')';
    return out;
}

Unit Unit::simplified(QList<UnitFactor> factors)
{
    QList<UnitFactor> merged = mergeFactors(expandPowers(factors));
    Dimension d = kNone;
    for (const UnitFactor &f : merged)
        addDimension(d, f.def->dimension, f.power);
    if (d == kNone)
        merged.clear(); // e.g. mph·h/km: the SI value already holds the plain number
    if (const std::optional<UnitFactor> alias = compoundAlias(merged))
        merged = {*alias};
    if (merged.size() >= 3) {
        for (const int derived : {N, J, W, Pa}) {
            if (unitAt(derived)->dimension == d) {
                merged = {UnitFactor{unitAt(derived), 1}};
                break;
            }
        }
    }
    Unit u;
    u.m_factors = std::move(merged);
    return u;
}

Unit Unit::product(const Unit &a, const Unit &b)
{
    return simplified(a.m_factors + b.m_factors);
}

Unit Unit::quotient(const Unit &a, const Unit &b)
{
    QList<UnitFactor> factors = a.m_factors;
    for (const UnitFactor &f : b.m_factors)
        factors.append({f.def, saturatedPower(-qint64(f.power))});
    return simplified(std::move(factors));
}

Unit Unit::power(int n) const
{
    if (n == 1 || isEmpty())
        return *this;
    QList<UnitFactor> factors;
    for (const UnitFactor &f : m_factors)
        factors.append({f.def, saturatedPower(qint64(f.power) * n)});
    return simplified(std::move(factors));
}

std::optional<Unit> Unit::root(int n) const
{
    if (n <= 0)
        return std::nullopt;
    QList<UnitFactor> factors;
    for (const UnitFactor &f : expandPowers(m_factors)) {
        if (f.power % n != 0)
            return std::nullopt;
        factors.append({f.def, f.power / n});
    }
    return simplified(std::move(factors));
}

QString superscript(int n)
{
    static constexpr char16_t kDigits[] = u"⁰¹²³⁴⁵⁶⁷⁸⁹";
    const QString plain = QString::number(n);
    QString out;
    for (const QChar c : plain) {
        if (c == u'-')
            out += QChar(0x207b);
        else
            out += QChar(kDigits[c.unicode() - u'0']);
    }
    return out;
}

} // namespace brulion::calc
