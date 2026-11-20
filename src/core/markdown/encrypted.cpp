#include "markdown/encrypted.h"

#include <array>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

namespace {

struct Prefix {
    QStringView text;
    EncryptedVersion version;
    bool hidden;
};

// In Meld Encrypt's order of evaluation (FeatureInplaceConstants._PREFIXES).
constexpr std::array<Prefix, 8> Prefixes = {{
        {u"%%\U0001F510γ ", EncryptedVersion::Gamma, true},
        {u"\U0001F510γ ", EncryptedVersion::Gamma, false},
        {u"%%\U0001F510β ", EncryptedVersion::Beta, true},
        {u"\U0001F510β ", EncryptedVersion::Beta, false},
        {u"%%\U0001F510α ", EncryptedVersion::Alpha, true},
        {u"\U0001F510α ", EncryptedVersion::Alpha, false},
        {u"%%\U0001F510 ", EncryptedVersion::Obsolete, true},
        {u"\U0001F510 ", EncryptedVersion::Obsolete, false},
}};

constexpr QStringView HiddenSuffix = u" \U0001F510%%";
constexpr QStringView VisibleSuffix = u" \U0001F510";

/// The fewest Base64 characters of a version's payload: its smallest ciphertext (an empty text:
/// the tag, plus the IV / salt / header the payload carries).
qsizetype minimumPayload(EncryptedVersion version)
{
    switch (version) {
    case EncryptedVersion::Obsolete: return 24; // 16-byte tag
    case EncryptedVersion::Alpha: return 44;    // 16-byte IV + tag
    case EncryptedVersion::Beta: return 64;     // 16-byte IV + 16-byte salt + tag
    case EncryptedVersion::Gamma: return 64;    // 32-byte header + tag
    }
    return 64;
}

bool isBase64Char(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'+' || u == u'/'
           || u == u'=';
}

bool isLineBreak(QChar c)
{
    return c == u'\n' || c == u'\r';
}

/// A fragment starting with `prefix` at `at` of `text` (the prefix matched there already).
std::optional<EncryptedSpan> fragmentAt(QStringView text, qsizetype at, const Prefix &prefix)
{
    const qsizetype contentFrom = at + prefix.text.size();
    // The content ends at the first 🔐 after the prefix (no marker may be inside it), on this line.
    qsizetype icon = -1;
    for (qsizetype i = contentFrom; i < text.size(); ++i) {
        if (isLineBreak(text[i]))
            return std::nullopt;
        if (text.sliced(i).startsWith(EncryptedIcon)) {
            icon = i;
            break;
        }
    }
    if (icon < 1)
        return std::nullopt;
    const QStringView suffix = prefix.hidden ? HiddenSuffix : VisibleSuffix;
    const qsizetype suffixAt = icon - 1; // the suffix's space
    if (suffixAt < contentFrom || !text.sliced(suffixAt).startsWith(suffix))
        return std::nullopt;
    EncryptedSpan span;
    span.from = at;
    span.to = suffixAt + suffix.size();
    span.version = prefix.version;
    span.hidden = prefix.hidden;
    qsizetype payloadFrom = contentFrom;
    if (text.sliced(contentFrom, suffixAt - contentFrom).startsWith(EncryptedHintMark)) {
        const qsizetype hintFrom = contentFrom + EncryptedHintMark.size();
        const qsizetype hintEnd = text.sliced(0, suffixAt).indexOf(EncryptedHintMark, hintFrom);
        if (hintEnd < 0)
            return std::nullopt; // an unclosed hint: no fragment (Meld Encrypt: invalid format)
        span.hint = text.sliced(hintFrom, hintEnd - hintFrom).toString();
        payloadFrom = hintEnd + EncryptedHintMark.size();
    }
    span.payloadFrom = payloadFrom;
    span.payloadTo = suffixAt;
    const QStringView payload = text.sliced(payloadFrom, suffixAt - payloadFrom);
    if (payload.size() < minimumPayload(prefix.version))
        return std::nullopt;
    for (const QChar c : payload) {
        if (!isBase64Char(c))
            return std::nullopt;
    }
    return span;
}

} // namespace

bool mayContainEncrypted(QStringView text)
{
    return text.contains(EncryptedIcon);
}

QList<EncryptedSpan> findEncryptedFragments(QStringView text)
{
    QList<EncryptedSpan> out;
    if (!mayContainEncrypted(text))
        return out;
    qsizetype i = 0;
    while (i < text.size()) {
        const qsizetype icon = text.indexOf(EncryptedIcon, i);
        if (icon < 0)
            break;
        // A prefix starts at the icon or two characters before it (`%%`).
        std::optional<EncryptedSpan> found;
        for (const qsizetype at : {icon - 2, icon}) {
            if (at < i || at < 0)
                continue;
            for (const Prefix &p : Prefixes) {
                if (text.sliced(at).startsWith(p.text)) {
                    found = fragmentAt(text, at, p);
                    if (found)
                        break;
                }
            }
            if (found)
                break;
        }
        if (found) {
            out.append(*found);
            i = found->to;
        } else {
            i = icon + EncryptedIcon.size();
        }
    }
    return out;
}

std::optional<EncryptedSpan> encryptedFragmentAt(QStringView text, qsizetype from, qsizetype to)
{
    if (from < 0 || to > text.size() || from >= to)
        return std::nullopt;
    for (const EncryptedSpan &span : findEncryptedFragments(text.sliced(from, to - from))) {
        if (span.from == 0 && span.to == to - from) {
            EncryptedSpan out = span;
            out.from += from;
            out.to += from;
            out.payloadFrom += from;
            out.payloadTo += from;
            return out;
        }
    }
    return std::nullopt;
}

QString encryptedFragmentText(EncryptedVersion version, bool hidden, QStringView hint, QStringView payload)
{
    QString out;
    for (const Prefix &p : Prefixes) {
        if (p.version == version && p.hidden == hidden) {
            out = p.text.toString();
            break;
        }
    }
    if (!hint.isEmpty())
        out += EncryptedHintMark + hint + EncryptedHintMark;
    out += payload;
    out += hidden ? HiddenSuffix : VisibleSuffix;
    return out;
}

bool isValidEncryptedHint(QStringView hint)
{
    return !hint.contains(EncryptedIcon) && !hint.contains(EncryptedHintMark) && !hint.contains(u'\n')
           && !hint.contains(u'\r');
}

QString maskEncryptedFragments(QStringView text)
{
    const QList<EncryptedSpan> spans = findEncryptedFragments(text);
    if (spans.isEmpty())
        return text.toString();
    QString out = text.toString();
    for (const EncryptedSpan &span : spans) {
        for (qsizetype i = span.from; i < span.to; ++i)
            out[i] = u' ';
    }
    return out;
}

bool isEncryptedNotePath(QStringView path)
{
    return path.endsWith(u".mdenc", Qt::CaseInsensitive) || path.endsWith(u".encrypted", Qt::CaseInsensitive);
}

} // namespace brulion::markdown
