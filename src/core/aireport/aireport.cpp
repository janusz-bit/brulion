#include "aireport/aireport.h"

#include "ai/ai.h"
#include "ai/local.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::aireport {

namespace {

/// `text` cut to at most `limit` characters (never inside a surrogate pair) with "…"; whole when
/// it fits or `limit` < 0. `cut` tells whether it was cut.
QString cutTo(const QString &text, int limit, bool *cut)
{
    if (limit < 0 || text.size() <= limit) {
        *cut = false;
        return text;
    }
    qsizetype n = limit;
    if (n > 0 && text.at(n - 1).isHighSurrogate())
        --n;
    *cut = true;
    return text.left(n) + u'…';
}

/// One line of a value the report lists: whitespace (line breaks too) collapsed, at most 200
/// characters; "not known" for none.
QString oneLine(const QString &value)
{
    QString s = value.simplified();
    if (s.size() > 200) {
        bool cut = false;
        s = cutTo(s, 200, &cut);
    }
    return s.isEmpty() ? u"not known"_s : s;
}

/// The lines of a section: its heading, the text fenced (or what stands for none) and, when it was
/// cut, how much of it there is.
QString section(const QString &heading, const QString &text, int limit, const QString &empty)
{
    QString out = u"### "_s + heading + u"\n\n"_s;
    if (text.trimmed().isEmpty())
        return out + empty + u'\n';
    bool cut = false;
    const QString kept = cutTo(text, limit, &cut);
    out += fenced(kept) + u'\n';
    if (cut)
        out += u"\n(Cut to fit the link: the first %1 of %2 characters.)\n"_s.arg(QString::number(limit),
                                                                                   QString::number(text.size()));
    return out;
}

QByteArray encoded(const QString &text) { return QUrl::toPercentEncoding(text); }

/// The link with each long text cut to `limit` characters (< 0: none cut).
Link make(const Report &report, int limit, QStringView address)
{
    Link l;
    l.email = !address.isEmpty();
    l.title = title(report);
    l.body = body(report, limit);
    QByteArray bytes;
    if (l.email) {
        QString crlf = l.body;
        crlf.replace(u"\n"_s, u"\r\n"_s); // (RFC 6068: a line break in a body is CRLF)
        bytes = "mailto:" + address.toLatin1() + "?subject=" + encoded(l.title) + "&body=" + encoded(crlf);
    } else {
        bytes = repositoryUrl().toLatin1() + "/issues/new?title=" + encoded(l.title) + "&body=" + encoded(l.body);
    }
    l.url = QUrl::fromEncoded(bytes, QUrl::StrictMode);
    const qsizetype longest = std::max({report.comment.size(), report.response.size(), report.question.size()});
    l.cut = limit >= 0 && limit < longest;
    return l;
}

qsizetype lengthOf(const Link &l) { return l.url.toEncoded().size(); }

/// The longest link of at most `maxLength` characters: the whole report, else its texts cut to
/// the longest length that fits (the shorter texts whole).
Link fitted(const Report &report, int maxLength, QStringView address)
{
    Link whole = make(report, -1, address);
    if (lengthOf(whole) <= maxLength)
        return whole;
    int lo = 0;
    int hi = int(std::max({report.comment.size(), report.response.size(), report.question.size()})) - 1;
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        if (lengthOf(make(report, mid, address)) <= maxLength)
            lo = mid;
        else
            hi = mid - 1;
    }
    Link best = make(report, std::max(lo, 0), address);
    best.cut = true;
    return best;
}

/// A code's part as place() keeps it: [a-z0-9-], at most 40 characters.
QString codePart(QStringView part)
{
    QString out;
    for (const QChar c : part) {
        const QChar l = c.toLower();
        if ((l >= u'a' && l <= u'z') || (l >= u'0' && l <= u'9') || l == u'-')
            out += l;
        if (out.size() >= 40)
            break;
    }
    return out;
}

} // namespace

const QList<Reason> &reasons()
{
    static const QList<Reason> all{Reason::Offensive,  Reason::Sexual,     Reason::Violent,
                                   Reason::Harassment, Reason::Misleading, Reason::Other};
    return all;
}

QString reasonId(Reason reason)
{
    switch (reason) {
    case Reason::Offensive: return u"offensive"_s;
    case Reason::Sexual: return u"sexual"_s;
    case Reason::Violent: return u"violent"_s;
    case Reason::Harassment: return u"harassment"_s;
    case Reason::Misleading: return u"misleading"_s;
    case Reason::Other: break;
    }
    return u"other"_s;
}

std::optional<Reason> reasonFromId(QStringView id)
{
    const QString wanted = id.trimmed().toString().toLower();
    for (const Reason r : reasons()) {
        if (reasonId(r) == wanted)
            return r;
    }
    return std::nullopt;
}

QString reasonText(Reason reason)
{
    switch (reason) {
    case Reason::Offensive: return u"Offensive or hateful"_s;
    case Reason::Sexual: return u"Sexual content"_s;
    case Reason::Violent: return u"Violent or dangerous"_s;
    case Reason::Harassment: return u"Harassment"_s;
    case Reason::Misleading: return u"False or misleading"_s;
    case Reason::Other: break;
    }
    return u"Other"_s;
}

QString repositoryUrl() { return u"https://github.com/janusz-bit/brulion"_s; }

QString title(const Report &report) { return u"AI response report: "_s + reasonText(report.reason); }

QString body(const Report &report, int limit)
{
    QString out = u"An AI response reported from Brulion (Report response).\n\n"_s;
    out += u"- Reason: "_s + reasonText(report.reason) + u'\n';
    out += u"- Where: "_s + oneLine(report.where) + u'\n';
    out += u"- Model: "_s + oneLine(report.model) + u'\n';
    out += u"- App: Brulion "_s + oneLine(report.version) + u'\n';
    out += u"- Device: "_s + oneLine(report.device) + u'\n';
    if (!report.comment.trimmed().isEmpty())
        out += u'\n' + section(u"Comment"_s, report.comment.trimmed(), limit, QString());
    out += u'\n' + section(u"The response"_s, report.response, limit, u"(The response is empty.)"_s);
    if (!report.question.trimmed().isEmpty())
        out += u'\n' + section(u"The question before it"_s, report.question, limit, QString());
    return out;
}

QString fenced(QStringView text)
{
    QString s = text.toString();
    s.replace(u"\r\n"_s, u"\n"_s);
    s.replace(u'\r', u'\n');
    while (s.endsWith(u'\n'))
        s.chop(1);
    qsizetype longest = 0;
    for (qsizetype i = 0; i < s.size();) {
        if (s.at(i) != u'`') {
            ++i;
            continue;
        }
        qsizetype run = 0;
        while (i + run < s.size() && s.at(i + run) == u'`')
            ++run;
        longest = std::max(longest, run);
        i += run;
    }
    const QString fence(std::max<qsizetype>(3, longest + 1), u'`');
    return fence + u'\n' + s + u'\n' + fence;
}

Link issueLink(const Report &report, int maxLength) { return fitted(report, maxLength, {}); }

Link emailLink(const Report &report, QStringView address, int maxLength)
{
    if (!isEmailAddress(address))
        return issueLink(report, maxLength);
    return fitted(report, maxLength, address.trimmed());
}

Link link(const Report &report, QStringView address, int maxLength)
{
    return isEmailAddress(address) ? emailLink(report, address, maxLength) : issueLink(report, maxLength);
}

bool isEmailAddress(QStringView address)
{
    static const QRegularExpression plain(
            u"^[A-Za-z0-9._+-]{1,64}@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$"_s);
    const QStringView a = address.trimmed();
    return a.size() <= 254 && plain.matchView(a).hasMatch();
}

QString place(QStringView code)
{
    const QList<QStringView> parts = code.trimmed().split(u'/');
    const QString kind = codePart(parts.value(0));
    if (kind == u"chat")
        return u"AI chat"_s;
    if (kind == u"writing") {
        const QString action = codePart(parts.value(1));
        const QString preset = codePart(parts.value(2));
        QString out = u"Writing assistant"_s;
        if (!action.isEmpty())
            out += u": "_s + action;
        if (!action.isEmpty() && !preset.isEmpty())
            out += u" ("_s + preset + u')';
        return out;
    }
    return u"AI response"_s;
}

QString modelDescription(const ai::Settings &settings)
{
    const QString provider = settings.provider.simplified().toLower();
    QString model = settings.model.simplified();
    if (model.isEmpty())
        model = ai::defaultModel(provider);
    if (model.size() > 120) {
        bool cut = false;
        model = cutTo(model, 120, &cut);
    }
    if (provider == u"local") {
        const ai::LocalModel *local = ai::localModel(model);
        return u"On this device · "_s + (local ? local->name : model.isEmpty() ? u"no model chosen"_s : model);
    }
    const QString chosen = model.isEmpty() ? u"no model chosen"_s : model;
    if (provider == u"anthropic")
        return u"Anthropic · "_s + chosen;
    if (provider == u"openai")
        return u"OpenAI-compatible API · "_s + chosen;
    return (provider.isEmpty() ? u"Unknown provider"_s : provider.left(40)) + u" · "_s + chosen;
}

} // namespace brulion::aireport
