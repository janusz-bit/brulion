#include "log/redact.h"

#include <QHash>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::logging {

namespace {

constexpr qsizetype kMinSecretLength = 4;

/// Key names whose values are secrets: any prefix, this ending (password, app_password, …).
constexpr auto kLongKeys = u"(?:password|passwd|pwd|passphrase|secret|token|apikey|api[-_]key|sessionid|session[-_]id"
                           u"|session|auth|signature|credentials?)";
/// Short names that are secret only on their own.
constexpr auto kShortKeys = u"(?:pass|sid|sig)";
/// Header names whose values are secrets.
constexpr auto kHeaders = u"(?:proxy-authorization|authorization|set-cookie|cookie|x-api-key|x-auth-token"
                          u"|x-csrf-token|requesttoken)";

struct Rule {
    QRegularExpression re;
    QString replacement;
};

QRegularExpression compile(const QString &pattern, bool caseInsensitive = true)
{
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (caseInsensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(pattern, options);
    Q_ASSERT_X(re.isValid(), "redact", qPrintable(re.errorString() + u": "_s + pattern));
    re.optimize();
    return re;
}

const QList<Rule> &rules()
{
    static const QList<Rule> list = [] {
        const QString longKeys = QString::fromUtf16(kLongKeys);
        const QString shortKeys = QString::fromUtf16(kShortKeys);
        const QString headers = QString::fromUtf16(kHeaders);
        const QString anyKey = u"(?:[\\w.-]*"_s + longKeys + u"|"_s + shortKeys + u")"_s;
        QList<Rule> r;
        // user:password@ in URLs (any scheme; the user name goes too).
        r.append({compile(u"(\\b[a-z][a-z0-9+.\\-]*://)[^\\s/?#@\\[\\]<>\"']+@"_s), u"\\1***@"_s});
        // Header lines: "Authorization: Basic …" -> "Authorization: ***" (to the end of the line).
        r.append({compile(u"(?<![\\w-])("_s + headers + u")(\\s*+:\\s*+)[^\\r\\n]*"_s), u"\\1\\2***"_s});
        // JSON / QDebug pairs: "authorization": "…", ("Authorization", "…"), "appPassword":"…".
        r.append({compile(u"(\"(?:"_s + headers + u"|"_s + anyKey + u")\"\\s*+[:,]\\s*+\")(?:[^\"\\\\]|\\\\.)*(\")"_s),
                  u"\\1***\\2"_s});
        // Auth schemes anywhere.
        r.append({compile(u"\\b(Basic|Bearer)(\\s++)[A-Za-z0-9+/=._~\\-]{8,}+"_s, false), u"\\1\\2***"_s});
        // Query parameters: "?token=abc&x=1" -> "?token=***&x=1" (OAuth codes too).
        r.append({compile(u"([?&;](?:"_s + anyKey + u"|code)=)[^&#\\s\"'<>]*+"_s), u"\\1***"_s});
        // key=value pairs outside URLs: to the next separator or the end of the line.
        r.append({compile(u"(?<![\\w?&;.\\-])("_s + anyKey + u")(\\s*+=\\s*+)(?!\\*\\*\\*)[^\\r\\n&;,\"'<>]*+"_s),
                  u"\\1\\2***"_s});
        // Nextcloud app passwords: five groups of five letters / digits.
        r.append({compile(u"(?<![A-Za-z0-9])[A-Za-z0-9]{5}(?:-[A-Za-z0-9]{5}){4}(?![A-Za-z0-9])"_s, false), u"***"_s});
        // JSON Web Tokens.
        r.append(
                {compile(u"\\beyJ[A-Za-z0-9_\\-]{4,}+\\.[A-Za-z0-9_\\-]{4,}+\\.[A-Za-z0-9_\\-]*+"_s, false), u"***"_s});
        return r;
    }();
    return list;
}

/// Cheap check: can any pattern match at all?
bool mayContainPattern(QStringView text)
{
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'@':
        case u'=':
        case u':':
        case u'"':
        case u'-': return true;
        default: break;
        }
    }
    return text.contains(u"Basic") || text.contains(u"Bearer") || text.contains(u"eyJ");
}

struct Registry {
    mutable QReadWriteLock lock;
    QHash<QString, int> counts;
    QStringList forms; ///< every registered form, longest first
};

Registry &registry()
{
    static Registry r;
    return r;
}

/// Plain, percent-encoded and Base64 forms of a secret.
QStringList formsOf(const QString &secret)
{
    QStringList forms{secret};
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(secret));
    if (encoded != secret)
        forms.append(encoded);
    const QString lowerEncoded = encoded.toLower();
    if (lowerEncoded != encoded && lowerEncoded != secret)
        forms.append(lowerEncoded);
    forms.append(QString::fromLatin1(secret.toUtf8().toBase64()));
    return forms;
}

void rebuildForms(Registry &r)
{
    QStringList forms;
    for (auto it = r.counts.cbegin(); it != r.counts.cend(); ++it) {
        for (const QString &form : formsOf(it.key())) {
            if (form.size() >= kMinSecretLength && !forms.contains(form))
                forms.append(form);
        }
    }
    std::sort(forms.begin(), forms.end(),
              [](const QString &a, const QString &b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
    r.forms = forms;
}

/// Lone surrogates make the regular expressions fail on the whole text: replace them.
void fixSurrogates(QString &text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            ++i;
            continue;
        }
        if (c.isSurrogate())
            text[i] = QChar::ReplacementCharacter;
    }
}

} // namespace

QString redact(QStringView text)
{
    QString out = text.toString();
    fixSurrogates(out);
    {
        const Registry &r = registry();
        QReadLocker locker(&r.lock);
        for (const QString &form : r.forms) {
            if (out.contains(form))
                out.replace(form, kMask.toString());
        }
    }
    if (!mayContainPattern(out))
        return out;
    for (const Rule &rule : rules())
        out.replace(rule.re, rule.replacement);
    return out;
}

void addSecret(const QString &secret)
{
    if (secret.size() < kMinSecretLength)
        return;
    Registry &r = registry();
    QWriteLocker locker(&r.lock);
    if (++r.counts[secret] == 1)
        rebuildForms(r);
}

void removeSecret(const QString &secret)
{
    if (secret.size() < kMinSecretLength)
        return;
    Registry &r = registry();
    QWriteLocker locker(&r.lock);
    const auto it = r.counts.find(secret);
    if (it == r.counts.end())
        return;
    if (--it.value() <= 0) {
        r.counts.erase(it);
        rebuildForms(r);
    }
}

void clearSecrets()
{
    Registry &r = registry();
    QWriteLocker locker(&r.lock);
    r.counts.clear();
    r.forms.clear();
}

int secretCount()
{
    const Registry &r = registry();
    QReadLocker locker(&r.lock);
    return int(r.counts.size());
}

ScopedSecret::ScopedSecret(QString secret) : m_secret(std::move(secret))
{
    addSecret(m_secret);
}

ScopedSecret::~ScopedSecret()
{
    removeSecret(m_secret);
}

ScopedSecret::ScopedSecret(ScopedSecret &&other) noexcept : m_secret(std::exchange(other.m_secret, QString())) { }

ScopedSecret &ScopedSecret::operator=(ScopedSecret &&other) noexcept
{
    if (this != &other) {
        removeSecret(m_secret);
        m_secret = std::exchange(other.m_secret, QString());
    }
    return *this;
}

void ScopedSecret::reset(const QString &secret)
{
    if (secret == m_secret)
        return;
    addSecret(secret);
    removeSecret(m_secret);
    m_secret = secret;
}

} // namespace brulion::logging
