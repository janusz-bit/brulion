// Physical units of the calc engine. Part of the calc module — see calc/calc.h for the overview.
//
// A quantity is stored as a number in SI base units plus the (compound) display unit it was
// written in, so arithmetic never loses precision and "1 km + 500 m" shows "1,5 km". Units are
// products of known units with integer powers (km·h⁻¹). Temperatures are linear (delta) units
// in arithmetic; their offsets (°C, °F) are applied only by explicit conversions ("20°C to °F").
//
// Known units (case-sensitive; Polish words accepted where listed):
//   length   mm cm dm m km µm in ft yd mi nmi (cal/cali = inch, mila/mile/mil = mile)
//   mass     mg g dag dkg kg t oz lb
//   time     ms s/sec/sek min h/godz d/dzień/dni week/tydzień/tyg month/miesiąc (30.4375 d)
//            year/rok/lat (365.25 d) — plus English/Polish inflections
//   area     mm2 cm2 dm2 m2 km2 ha ar (a after a number)   volume  mm3 cm3 dm3 m3 ml cl dl l gal
//   speed    mph (and any compound: km/h, m/s)
//   data     b bit B kB KB MB GB TB KiB MiB GiB TiB kb Mb Gb
//   energy   J kJ MJ Wh kWh MWh kcal      power   W kW MW KM hp
//   force    N kN      pressure Pa hPa kPa MPa bar      frequency Hz kHz MHz GHz
//   temp.    K C/°C/degC F/°F/degF        angle   rad deg/°
#pragma once

#include "calc/calclocale.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <array>
#include <optional>

namespace brulion::calc {

/// Number of base dimensions: length, mass, time, temperature, data (bits), angle.
inline constexpr int kDimensionCount = 6;
/// Exponents of the base dimensions (length, mass, time, temperature, data, angle).
using Dimension = std::array<int, kDimensionCount>;

/// A known unit.
struct UnitDef {
    QStringView symbol;   ///< display symbol (English and default)
    QStringView symbolPl; ///< Polish display symbol, empty when the same
    Dimension dimension{};
    double factor = 1;    ///< value of one unit in SI base units (bits for data, radians for angles)
    double offset = 0;    ///< affine offset in kelvin (°C, °F); used only by conversions
    int powerOf = -1;     ///< for km2, cm3, ...: index of the unit this is a power of
    int power = 1;        ///< ... and that power

    QStringView displaySymbol(CalcLocale locale) const
    {
        return locale == CalcLocale::Polish && !symbolPl.isEmpty() ? symbolPl : symbol;
    }
    bool isTemperature() const;
};

/// The unit called `name` ("km", "godz", "°C"), or nullptr.
const UnitDef *findUnit(QStringView name);
/// Like findUnit(), plus units whose names are too common as variable names to be units
/// everywhere — `a` (are) — recognised only right after a number ("5 a") or after `to`.
const UnitDef *findQuantityUnit(QStringView name);

/// One factor of a compound unit.
struct UnitFactor {
    const UnitDef *def = nullptr;
    int power = 1;

    friend bool operator==(const UnitFactor &a, const UnitFactor &b)
    {
        return a.def == b.def && a.power == b.power;
    }
};

/// A compound unit: a product of unit powers. Empty = dimensionless.
class Unit {
public:
    Unit() = default;
    explicit Unit(const UnitDef *def);

    bool isEmpty() const { return m_factors.isEmpty(); }
    const QList<UnitFactor> &factors() const { return m_factors; }
    /// Sum of the factors' dimensions.
    Dimension dimension() const;
    bool isDimensionless() const;
    /// SI value of one of this unit (product of factor^power).
    double scale() const;
    /// The unit when it is a single unit with power 1 (e.g. for affine temperature conversion).
    const UnitDef *single() const;
    /// "km/h", "m/s²", "kg·m²", "s⁻¹", "°C".
    QString toString(CalcLocale locale) const;

    /// Simplified product / quotient: equal units and units of the same dimension merge into
    /// the first one (km·m -> km², h⁻¹·min -> dimensionless), W·h -> Wh, and products of three or
    /// more factors that form a force/energy/power/pressure become N/J/W/Pa.
    static Unit product(const Unit &a, const Unit &b);
    static Unit quotient(const Unit &a, const Unit &b);
    /// The unit raised to an integer power (simplified). Powers saturate at ±2^20 instead of
    /// overflowing; the calc engine itself never goes beyond ±99.
    Unit power(int n) const;
    /// The n-th root, when every power is divisible by n (m² -> m), else nullopt.
    std::optional<Unit> root(int n) const;

    friend bool operator==(const Unit &a, const Unit &b) { return a.m_factors == b.m_factors; }

private:
    static Unit simplified(QList<UnitFactor> factors);
    QList<UnitFactor> m_factors;
};

/// Superscript form of an integer exponent: 2 -> "²", -1 -> "⁻¹".
QString superscript(int n);

} // namespace brulion::calc
