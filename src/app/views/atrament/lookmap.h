// A view's look from AtramentView.viewLook (a map of its fields by name: colours as QColor or a
// name, numbers): what the map lacks keeps the default it is given.
#pragma once

#include "base/color.h"

#include <QColor>
#include <QString>
#include <QVariantMap>

#include <cstddef>
#include <utility>

namespace brulion::views::atr {

inline atrament::Color colorAt(const QVariantMap &look, const QString &key, atrament::Color fallback)
{
    const QColor c = look.value(key).value<QColor>();
    return c.isValid() ? atrament::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())} : fallback;
}

inline float numberAt(const QVariantMap &look, const QString &key, float fallback)
{
    return look.value(key, fallback).toFloat();
}

// A look's colour or size field by name (each look's table: its `colorFields`, `sizeFields`).
template<class Look>
using ColorField = std::pair<const char *, atrament::Color Look::*>;
template<class Look>
using SizeField = std::pair<const char *, float Look::*>;

// `Look` from the map: its tables' fields, its uiFamily; the rest its defaults.
template<class Look, std::size_t C, std::size_t S>
Look lookFrom(const QVariantMap &look, const ColorField<Look> (&colors)[C], const SizeField<Look> (&sizes)[S])
{
    Look t;
    for (const auto &[key, field] : colors)
        t.*field = colorAt(look, QString::fromLatin1(key), t.*field);
    for (const auto &[key, field] : sizes)
        t.*field = numberAt(look, QString::fromLatin1(key), t.*field);
    t.uiFamily = look.value(QStringLiteral("uiFamily")).toString().toStdU16String();
    return t;
}

} // namespace brulion::views::atr
