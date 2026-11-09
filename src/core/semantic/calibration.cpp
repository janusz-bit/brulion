#include "semantic/calibration.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

using namespace Qt::StringLiterals;

namespace brulion::semantic {

void normalize(Vector &v)
{
    double s = 0;
    for (float x : std::as_const(v))
        s += double(x) * x;
    if (s <= 0)
        return;
    const float inv = float(1.0 / std::sqrt(s));
    for (float &x : v)
        x *= inv;
}

float dot(const Vector &a, const Vector &b)
{
    return a.size() == b.size() ? std::inner_product(a.cbegin(), a.cend(), b.cbegin(), 0.0f) : 0.0f;
}

bool Calibration::appliesTo(QStringView query) const
{
    if (isEmpty())
        return false;
    int words = 0;
    for ([[maybe_unused]] QStringView w : query.tokenize(u' ', Qt::SkipEmptyParts)) {
        if (++words > maxQueryWords)
            return false;
    }
    return true;
}

Vector Calibration::apply(const Vector &v, bool query) const
{
    Vector out = v;
    const Vector &mean = query ? queryMean : passageMean;
    if (mean.size() == out.size())
        std::ranges::transform(out, mean, out.begin(), std::minus<>());
    if (direction.size() == out.size()) {
        const float p = dot(out, direction);
        std::ranges::transform(out, direction, out.begin(), [p](float x, float d) { return x - p * d; });
    }
    normalize(out);
    return out;
}

Calibration Calibration::estimate(const QList<Vector> &passages, const QList<bool> &english,
                                  const Vector &queryMean, int minPerLanguage)
{
    Calibration c;
    c.queryMean = queryMean;
    if (passages.isEmpty())
        return c;
    const qsizetype dim = passages.first().size();
    std::vector<double> all(size_t(dim), 0.0), pl(size_t(dim), 0.0), en(size_t(dim), 0.0);
    int nPl = 0, nEn = 0;
    for (qsizetype k = 0; k < passages.size(); ++k) {
        const Vector &v = passages.at(k);
        if (v.size() != dim)
            continue;
        const bool isEn = k < english.size() && english.at(k);
        std::vector<double> &lang = isEn ? en : pl;
        (isEn ? nEn : nPl)++;
        for (qsizetype i = 0; i < dim; ++i) {
            all[size_t(i)] += v.at(i);
            lang[size_t(i)] += v.at(i);
        }
    }
    const int n = nPl + nEn; // > 0: the first passage always counts
    c.passageMean.resize(dim);
    for (qsizetype i = 0; i < dim; ++i)
        c.passageMean[i] = float(all[size_t(i)] / n);
    // A language without chunks has no mean: one chunk of each at least, whatever minPerLanguage says.
    if (std::min(nPl, nEn) >= std::max(1, minPerLanguage)) {
        c.direction.resize(dim);
        for (qsizetype i = 0; i < dim; ++i)
            c.direction[i] = float(pl[size_t(i)] / nPl - en[size_t(i)] / nEn);
        normalize(c.direction);
    }
    return c;
}

namespace {

QJsonArray toArray(const Vector &v)
{
    QJsonArray a;
    for (float x : v)
        a.append(double(x));
    return a;
}

Vector fromArray(const QJsonValue &value)
{
    Vector v;
    const QJsonArray a = value.toArray();
    v.reserve(a.size());
    for (const auto &x : a)
        v.append(float(x.toDouble()));
    return v;
}

} // namespace

QJsonObject Calibration::toJson() const
{
    QJsonObject o;
    if (!queryMean.isEmpty())
        o.insert(u"queryMean"_s, toArray(queryMean));
    if (!passageMean.isEmpty())
        o.insert(u"passageMean"_s, toArray(passageMean));
    if (!direction.isEmpty())
        o.insert(u"direction"_s, toArray(direction));
    o.insert(u"maxQueryWords"_s, maxQueryWords);
    return o;
}

Calibration Calibration::fromJson(const QJsonObject &o)
{
    Calibration c;
    c.queryMean = fromArray(o.value(u"queryMean"_s));
    c.passageMean = fromArray(o.value(u"passageMean"_s));
    c.direction = fromArray(o.value(u"direction"_s));
    normalize(c.direction);
    if (o.contains(u"maxQueryWords"_s))
        c.maxQueryWords = o.value(u"maxQueryWords"_s).toInt();
    return c;
}

} // namespace brulion::semantic
