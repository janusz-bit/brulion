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
    if (passages.isEmpty()) {
        Calibration c;
        c.queryMean = queryMean;
        return c;
    }
    const qsizetype dim = passages.first().size();
    return estimate(
            passages.size(), int(dim),
            [&passages, dim](qsizetype k) { return passages.at(k).size() == dim ? passages.at(k).constData() : nullptr; },
            [&english](qsizetype k) { return k < english.size() && english.at(k); }, queryMean, minPerLanguage);
}

Calibration Calibration::estimate(qsizetype count, int dim, const std::function<const float *(qsizetype)> &vector,
                                  const std::function<bool(qsizetype)> &english, const Vector &queryMean,
                                  int minPerLanguage)
{
    Calibration c;
    c.queryMean = queryMean;
    if (count <= 0 || dim <= 0)
        return c;
    const size_t d = size_t(dim);
    std::vector<double> all(d, 0.0), pl(d, 0.0), en(d, 0.0);
    int nPl = 0, nEn = 0;
    for (qsizetype k = 0; k < count; ++k) {
        const float *v = vector(k);
        if (!v)
            continue;
        const bool isEn = english(k);
        std::vector<double> &lang = isEn ? en : pl;
        (isEn ? nEn : nPl)++;
        for (size_t i = 0; i < d; ++i) {
            all[i] += v[i];
            lang[i] += v[i];
        }
    }
    const int n = nPl + nEn;
    if (n == 0)
        return c;
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
