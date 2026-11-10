#include "qttranslations.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QTranslator>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ui {

QtTranslations::QtTranslations() = default;

QtTranslations::~QtTranslations()
{
    setLanguage(QString());
}

QStringList QtTranslations::catalogs()
{
    return {u"qtbase"_s, u"qtdeclarative"_s};
}

int QtTranslations::setLanguage(const QString &language)
{
    for (const Loaded &l : m_loaded)
        QCoreApplication::removeTranslator(l.translator.get());
    m_loaded.clear();
    // Qt's source texts are English: nothing to load for it.
    if (language.isEmpty() || language.startsWith(u"en"))
        return 0;
    const QString lang = language.left(2);
    const QStringList folders{u":/i18n/qt"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    for (const QStringList names = catalogs(); const QString &name : names) {
        for (const QString &folder : folders) {
            const QString file = folder + u'/' + name + u'_' + lang + u".qm"_s;
            if (!QFileInfo::exists(file))
                continue;
            auto translator = std::make_unique<QTranslator>();
            if (!translator->load(file) || translator->isEmpty())
                continue;
            QCoreApplication::installTranslator(translator.get());
            m_loaded.push_back(Loaded{name, file, std::move(translator)});
            break;
        }
    }
    return int(m_loaded.size());
}

QString QtTranslations::loadedFrom(const QString &name) const
{
    const auto it = std::ranges::find(m_loaded, name, &Loaded::name);
    return it != m_loaded.end() ? it->file : QString();
}

} // namespace brulion::ui
