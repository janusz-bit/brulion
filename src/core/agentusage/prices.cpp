// The API prices the usage history estimates costs with (usage.h: priceOf()). One table, dated by
// PricesAsOf: update both together.
//
// Sources (checked 2026-11-24):
// * Anthropic — the Claude API's pricing (platform.claude.com, the models overview): input and
//   output per model; prompt cache writes 1.25× the input price for five minutes, 2× for an hour;
//   cache reads 0.1× the input price except where a model has its own (Fable 5.2 and 5.1, Opus 5.5,
//   Sonnet 5.5, Fable 5); fast mode 2× (Opus 5.5, Opus 5, Opus 4.8, Fable 5.2). Sonnet 4 and 4.5
//   above 200 K prompt tokens (the 1M context beta) 2× input and 1.5× output; Haiku 5.5 above 100 K
//   5×.
// * OpenAI — developers.openai.com/api/docs/pricing, the Standard tier (input, cached input,
//   output) and Fast mode (formerly priority processing: its factor over Standard); above 272 K
//   prompt tokens the long-context rates. Codex models no longer on the page keep the prices they
//   had there (gpt-5-codex, gpt-5.1-codex*, gpt-5.2-codex, codex-mini-latest).

#include "agentusage/usage.h"

#include <QHash>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::agentusage {

namespace {

struct Entry {
    const char16_t *id;
    Price price;
};

/// Anthropic: cache writes from the input price, the cache read given.
constexpr Rates claude(double input, double output, double cacheRead)
{
    return Rates{input, output, input * 1.25, input * 2.0, cacheRead};
}
/// OpenAI: no cache writes; a model without a cached price reads at the input price.
constexpr Rates openai(double input, double cached, double output)
{
    return Rates{input, output, 0, 0, cached};
}
constexpr Price plain(Rates rates, double fast = 1)
{
    return Price{rates, 0, {}, fast};
}
constexpr Price tiered(Rates rates, qint64 above, Rates longRates, double fast = 1)
{
    return Price{rates, above, longRates, fast};
}

constexpr qint64 OpenAiLong = 272000;

const Entry Table[] = {
        // ---- Anthropic (USD per million tokens: input, output, cache read)
        {u"claude-fable-5-2", plain(claude(8, 40, 0.20), 2)},
        {u"claude-mythos-5-2", plain(claude(8, 40, 0.20), 2)},
        {u"claude-fable-5-1", plain(claude(10, 50, 0.25))},
        {u"claude-mythos-5-1", plain(claude(10, 50, 0.25))},
        {u"claude-fable-5", plain(claude(10, 50, 1.00))},
        {u"claude-mythos-5", plain(claude(10, 50, 1.00))},
        {u"claude-opus-5-5", plain(claude(4, 20, 0.20), 2)},
        {u"claude-opus-5", plain(claude(5, 25, 0.50), 2)},
        {u"claude-opus-4-8", plain(claude(5, 25, 0.50), 2)},
        {u"claude-opus-4-7", plain(claude(5, 25, 0.50))},
        {u"claude-opus-4-6", plain(claude(5, 25, 0.50))},
        {u"claude-opus-4-5", plain(claude(5, 25, 0.50))},
        {u"claude-opus-4-1", plain(claude(15, 75, 1.50))},
        {u"claude-opus-4", plain(claude(15, 75, 1.50))},
        {u"claude-3-opus", plain(claude(15, 75, 1.50))},
        {u"claude-sonnet-5-5", plain(claude(2, 10, 0.10))},
        {u"claude-sonnet-5", plain(claude(2, 10, 0.20))},
        {u"claude-sonnet-4-6", plain(claude(3, 15, 0.30))},
        {u"claude-sonnet-4-5", tiered(claude(3, 15, 0.30), 200000, claude(6, 22.5, 0.60))},
        {u"claude-sonnet-4", tiered(claude(3, 15, 0.30), 200000, claude(6, 22.5, 0.60))},
        {u"claude-3-7-sonnet", plain(claude(3, 15, 0.30))},
        {u"claude-3-5-sonnet", plain(claude(3, 15, 0.30))},
        {u"claude-haiku-5-5", tiered(claude(0.10, 0.50, 0.01), 100000, claude(0.50, 2.50, 0.05))},
        {u"claude-haiku-4-5", plain(claude(1, 5, 0.10))},
        {u"claude-3-5-haiku", plain(claude(0.80, 4, 0.08))},
        {u"claude-3-haiku", plain(claude(0.25, 1.25, 0.03))},
        // Claude Code's own messages (an error, a cancelled turn): no request was made.
        {u"<synthetic>", plain(Rates{})},

        // ---- OpenAI (USD per million tokens: input, cached input, output)
        {u"gpt-6.2-astra", tiered(openai(8, 0.40, 40), OpenAiLong, openai(16, 0.80, 60), 2)},
        {u"gpt-6.2-sol", tiered(openai(2, 0.10, 10), OpenAiLong, openai(4, 0.20, 15), 2)},
        {u"gpt-6.1-sol", tiered(openai(2, 0.10, 10), OpenAiLong, openai(4, 0.20, 15), 2)},
        {u"gpt-6.1-terra", tiered(openai(1, 0.05, 5), OpenAiLong, openai(2, 0.10, 7.5), 2)},
        {u"gpt-6-astra", tiered(openai(10, 1, 50), OpenAiLong, openai(20, 2, 75), 2)},
        {u"gpt-6-sol", tiered(openai(2, 0.20, 10), OpenAiLong, openai(4, 0.40, 15), 2)},
        {u"gpt-6-luna", tiered(openai(0.10, 0.01, 0.50), OpenAiLong, openai(0.20, 0.02, 0.75), 2)},
        {u"gpt-5.6-sol", tiered(openai(4, 0.40, 20), OpenAiLong, openai(8, 0.80, 30), 2)},
        {u"gpt-5.6-terra", tiered(openai(2, 0.20, 12), OpenAiLong, openai(4, 0.40, 18), 2)},
        {u"gpt-5.6-luna", tiered(openai(0.20, 0.02, 1.20), OpenAiLong, openai(0.40, 0.04, 1.80), 2)},
        {u"gpt-5.5", tiered(openai(5, 0.50, 30), OpenAiLong, openai(10, 1, 45), 2.5)},
        {u"gpt-5.5-pro", tiered(openai(30, 30, 180), OpenAiLong, openai(60, 60, 270))},
        {u"gpt-5.4", tiered(openai(2.50, 0.25, 15), OpenAiLong, openai(5, 0.50, 22.5), 2)},
        {u"gpt-5.4-pro", tiered(openai(30, 30, 180), OpenAiLong, openai(60, 60, 270))},
        {u"gpt-5.4-mini", plain(openai(0.75, 0.075, 4.50), 2)},
        {u"gpt-5.4-nano", plain(openai(0.20, 0.02, 1.25))},
        {u"gpt-5.3-codex", plain(openai(1.75, 0.175, 14), 2)},
        {u"gpt-5.2", plain(openai(1.75, 0.175, 14), 2)},
        {u"gpt-5.2-codex", plain(openai(1.75, 0.175, 14), 2)},
        {u"gpt-5.2-pro", plain(openai(21, 21, 168))},
        {u"gpt-5.1", plain(openai(1.25, 0.125, 10), 2)},
        {u"gpt-5.1-codex", plain(openai(1.25, 0.125, 10), 2)},
        {u"gpt-5.1-codex-max", plain(openai(1.25, 0.125, 10), 2)},
        {u"gpt-5.1-codex-mini", plain(openai(0.25, 0.025, 2))},
        {u"gpt-5", plain(openai(1.25, 0.125, 10), 2)},
        {u"gpt-5-codex", plain(openai(1.25, 0.125, 10), 2)},
        {u"gpt-5-mini", plain(openai(0.25, 0.025, 2), 1.8)},
        {u"gpt-5-nano", plain(openai(0.05, 0.005, 0.40))},
        {u"gpt-5-pro", plain(openai(15, 15, 120))},
        {u"codex-mini-latest", plain(openai(1.50, 0.375, 6))},
};

const QHash<QString, Price> &table()
{
    static const QHash<QString, Price> prices = [] {
        QHash<QString, Price> out;
        for (const Entry &e : Table)
            out.insert(QString::fromUtf16(e.id), e.price);
        return out;
    }();
    return prices;
}

} // namespace

QString normalizedModel(const QString &model)
{
    QString m = model.trimmed().toLower();
    // "claude-sonnet-4-5[1m]": Claude Code's choice of the long context.
    if (const qsizetype bracket = m.indexOf(u'['); bracket > 0)
        m.truncate(bracket);
    // "openai/gpt-5", "us.anthropic.claude-opus-4-1-20250805-v1:0" (Bedrock), "claude-opus-4-5@20251101" (Vertex).
    if (const qsizetype slash = m.lastIndexOf(u'/'); slash >= 0)
        m = m.mid(slash + 1);
    if (const qsizetype vendor = m.indexOf(u"anthropic."); vendor >= 0)
        m = m.mid(vendor + 10);
    if (const qsizetype at = m.indexOf(u'@'); at > 0)
        m.truncate(at);
    static const QRegularExpression suffix(u"(-v\\d+(:\\d+)?|-latest|-\\d{8}|-\\d{4}-\\d{2}-\\d{2})$"_s);
    for (int i = 0; i < 3; ++i) {
        const QRegularExpressionMatch match = suffix.match(m);
        if (!match.hasMatch() || match.capturedStart() == 0 || (m == u"codex-mini-latest"))
            break;
        m.truncate(match.capturedStart());
    }
    return m;
}

std::optional<Price> priceOf(const QString &model)
{
    const QString id = normalizedModel(model);
    if (id.isEmpty())
        return std::nullopt;
    const auto it = table().constFind(id);
    if (it == table().constEnd())
        return std::nullopt;
    return *it;
}

double costOf(const Tokens &tokens, const Price &price, quint8 flags)
{
    const Rates &r = (flags & LongContext) && price.longAbove > 0 ? price.longRates : price.rates;
    const double factor = (flags & Fast) ? price.fastFactor : 1.0;
    const double micro = double(tokens.input) * r.input + double(tokens.output) * r.output
                         + double(tokens.cacheWrite5m) * r.cacheWrite5m + double(tokens.cacheWrite1h) * r.cacheWrite1h
                         + double(tokens.cacheRead) * r.cacheRead;
    return factor * micro / 1e6;
}

qint64 promptTokens(const Tokens &tokens)
{
    return tokens.input + tokens.cacheWrite5m + tokens.cacheWrite1h + tokens.cacheRead;
}

} // namespace brulion::agentusage
