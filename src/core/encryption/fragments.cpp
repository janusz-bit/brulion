#include "encryption/fragments.h"

namespace brulion::encryption {

QString encryptFragment(QStringView plaintext, const crypto::SecureBytes &password, QStringView hint, bool hidden,
                        const crypto::RandomSource &random)
{
    if (!markdown::isValidEncryptedHint(hint))
        return {};
    return markdown::encryptedFragmentText(markdown::EncryptedVersion::Gamma, hidden, hint,
                                           encryptPayload(plaintext, password, random));
}

Decrypted decryptFragment(QStringView text, const markdown::EncryptedSpan &span, const crypto::SecureBytes &password)
{
    if (span.payloadFrom < 0 || span.payloadTo > text.size() || span.payloadFrom > span.payloadTo)
        return {Failure::Malformed, {}};
    return decryptPayload(span.version, text.sliced(span.payloadFrom, span.payloadTo - span.payloadFrom), password);
}

QString reencryptFragment(const markdown::EncryptedSpan &span, QStringView plaintext,
                          const crypto::SecureBytes &password, const crypto::RandomSource &random)
{
    return encryptFragment(plaintext, password, span.hint, span.hidden, random);
}

QString replaceRange(QStringView text, qsizetype from, qsizetype to, QStringView replacement)
{
    from = qBound<qsizetype>(0, from, text.size());
    to = qBound(from, to, text.size());
    QString out;
    out.reserve(text.size() - (to - from) + replacement.size());
    out.append(text.first(from));
    out.append(replacement);
    out.append(text.sliced(to));
    return out;
}

std::optional<QString> changeFragmentPassword(QStringView text, const markdown::EncryptedSpan &span,
                                              const crypto::SecureBytes &oldPassword,
                                              const crypto::SecureBytes &newPassword, QStringView newHint,
                                              Failure *failure, const crypto::RandomSource &random)
{
    Decrypted plain = decryptFragment(text, span, oldPassword);
    if (failure)
        *failure = plain.failure;
    if (!plain.ok())
        return std::nullopt;
    const QString fragment = encryptFragment(plain.text, newPassword, newHint, span.hidden, random);
    crypto::wipe(plain.text);
    if (fragment.isEmpty()) {
        if (failure)
            *failure = Failure::Malformed;
        return std::nullopt;
    }
    return replaceRange(text, span.from, span.to, fragment);
}

std::optional<QString> decryptInPlace(QStringView text, const markdown::EncryptedSpan &span,
                                      const crypto::SecureBytes &password, Failure *failure)
{
    Decrypted plain = decryptFragment(text, span, password);
    if (failure)
        *failure = plain.failure;
    if (!plain.ok())
        return std::nullopt;
    QString out = replaceRange(text, span.from, span.to, plain.text);
    crypto::wipe(plain.text);
    return out;
}

} // namespace brulion::encryption
