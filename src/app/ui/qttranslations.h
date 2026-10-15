// QtTranslations — Qt's own texts in the UI language: the standard buttons of dialogs ("Anuluj",
// "Otwórz"), the non-native file dialog of QtQuick.Dialogs and other strings of Qt's modules come
// from Qt's catalogs, not from Brulion's (i18n/brulion_pl.ts). The catalogs of `catalogs()` are
// bundled as resources under ":/i18n/qt/" from Qt's translations folder at build time
// (src/app/ui/CMakeLists.txt; nix: qt6.qttranslations), so the desktop, Android and the browser
// load the same files; Qt's installed translations (QLibraryInfo::TranslationsPath) are the
// fallback when a build has none bundled.
//
//   QtTranslations qt;  qt.setLanguage("pl");   // at start-up and on every language switch
#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QTranslator;

namespace brulion::ui {

class QtTranslations {
public:
    QtTranslations();
    ~QtTranslations();
    QtTranslations(const QtTranslations &) = delete;
    QtTranslations &operator=(const QtTranslations &) = delete;

    /// Qt's catalogs used by the app ("qtbase", "qtdeclarative").
    static QStringList catalogs();
    /// Install Qt's catalogs for `language` ("pl"; "en" or anything else: none — Qt's texts are
    /// English), replacing the ones installed before. Returns the number of catalogs loaded.
    int setLanguage(const QString &language);
    /// Where the catalog `name` of `language` was loaded from last ("" when it was not found).
    QString loadedFrom(const QString &name) const;

private:
    struct Loaded {
        QString name;
        QString file;
        std::unique_ptr<QTranslator> translator;
    };
    std::vector<Loaded> m_loaded;
};

} // namespace brulion::ui
