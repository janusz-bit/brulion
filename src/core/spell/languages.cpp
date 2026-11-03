#include "spell/languages.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::spell {

QString primaryLanguage(const QString &name)
{
    static const QRegularExpression re(u"^\\s*([A-Za-z]{2,3})(?=$|[-_@.\\s])"_s);
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QStringList resolveLanguages(const QString &setting, const QString &uiLanguage, const QString &systemLanguage,
                             const QStringList &available)
{
    QStringList have;
    for (const QString &a : available) {
        const QString p = primaryLanguage(a);
        if (!p.isEmpty() && !have.contains(p))
            have.append(p);
    }
    QStringList wanted;
    const QString trimmed = setting.trimmed().toLower();
    if (trimmed.isEmpty() || trimmed == u"auto") {
        wanted = {primaryLanguage(uiLanguage), primaryLanguage(systemLanguage), u"en"_s};
    } else {
        for (const QString &part : trimmed.split(u',', Qt::SkipEmptyParts))
            wanted.append(primaryLanguage(part));
        if (std::all_of(wanted.cbegin(), wanted.cend(), [](const QString &w) { return w.isEmpty(); }))
            wanted = {primaryLanguage(uiLanguage), primaryLanguage(systemLanguage), u"en"_s};
    }
    QStringList out;
    for (const QString &w : std::as_const(wanted)) {
        if (!w.isEmpty() && have.contains(w) && !out.contains(w))
            out.append(w);
    }
    return out;
}

} // namespace brulion::spell
