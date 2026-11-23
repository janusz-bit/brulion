#include "automation/chatlink.h"

#include "automation/uri.h"

#include <QChar>
#include <QList>

using namespace Qt::StringLiterals;

namespace brulion::automation {

namespace {

constexpr char32_t kZeroWidthJoiner = 0x200D;

bool isVariationSelector(char32_t code)
{
    return (code >= 0xFE00 && code <= 0xFE0F) || (code >= 0xE0100 && code <= 0xE01EF);
}

/// A character an emoji sequence is made of (pictographs, dingbats, arrows and the like): a
/// zero-width joiner between two of them is the emoji's own (a family: man, ZWJ, woman, ZWJ, girl),
/// not something hidden.
bool isEmojiLike(char32_t code)
{
    return code >= 0x1F000 || (code >= 0x2190 && code <= 0x2BFF) || (code >= 0x2300 && code <= 0x23FF)
           || code == 0x00A9 || code == 0x00AE || code == 0x203C || code == 0x2049 || code == 0x2122
           || code == 0x3030 || code == 0x303D || code == 0x3297 || code == 0x3299;
}

} // namespace

bool isHiddenCharacter(char32_t code)
{
    if (isVariationSelector(code))
        return true;
    // Hangul fillers: letters that draw nothing.
    if (code == 0x115F || code == 0x1160 || code == 0x3164 || code == 0xFFA0)
        return true;
    return QChar::category(code) == QChar::Other_Format;
}

CleanedPrompt cleanPrompt(QStringView prompt, qsizetype limit)
{
    // Code points first: a tag character or a selector of the supplementary planes is a pair.
    const QList<uint> codes = prompt.toUcs4();
    CleanedPrompt out;
    QList<char32_t> kept;
    kept.reserve(codes.size());
    for (qsizetype i = 0; i < codes.size(); ++i) {
        const char32_t code = codes.at(i);
        if (code == 0x2028 || code == 0x2029) { // line and paragraph separators: a line break
            kept.append(u'\n');
            continue;
        }
        if (!isHiddenCharacter(code)) {
            kept.append(code);
            continue;
        }
        const char32_t before = kept.isEmpty() ? 0 : kept.last();
        // One text or emoji presentation selector after a visible character: the emoji's own.
        if ((code == 0xFE0E || code == 0xFE0F) && before != 0 && !isHiddenCharacter(before)
            && !QChar::isSpace(before)) {
            kept.append(code);
            continue;
        }
        // A joiner between two emoji (the one before may end in its presentation selector).
        if (code == kZeroWidthJoiner && i + 1 < codes.size() && isEmojiLike(codes.at(i + 1))) {
            const char32_t base = before == 0xFE0F && kept.size() > 1 ? kept.at(kept.size() - 2) : before;
            if (isEmojiLike(base)) {
                kept.append(code);
                continue;
            }
        }
        ++out.hidden;
    }
    QString text = QString::fromUcs4(kept.constData(), kept.size()).trimmed();
    if (text.size() > limit) {
        qsizetype cut = limit;
        if (cut > 0 && QChar::isHighSurrogate(text.at(cut - 1).unicode()))
            --cut;
        text.truncate(cut);
        out.capped = true;
    }
    out.text = text;
    return out;
}

QString chatLinkUri(const QString &prompt, const QString &partner, const QString &page)
{
    Request r;
    r.action = Action::Chat;
    if (const QString text = cleanPrompt(prompt).text; !text.isEmpty())
        r.params.append({u"prompt"_s, text});
    if (const QString who = cleanPrompt(partner, 200).text; !who.isEmpty())
        r.params.append({u"partner"_s, who});
    if (const QString about = cleanPrompt(page, 500).text; !about.isEmpty())
        r.params.append({u"page"_s, about});
    return buildUri(r);
}

} // namespace brulion::automation
