// Where OCR's engine and models are looked for (ocrdata.h).
#include "ocrdata.h"

#include <QDir>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

void addUnique(QStringList &out, const QString &entry)
{
    if (!entry.isEmpty() && !out.contains(entry))
        out.append(entry);
}

} // namespace

QStringList tesseractLibraryCandidates(const QString &envLibrary, const QString &builtIn, bool windows,
                                       const QStringList &windowsDirs)
{
    QStringList out;
    const QString env = envLibrary.trimmed();
    if (env.compare(u"none"_s, Qt::CaseInsensitive) == 0)
        return out;
    addUnique(out, env);
    addUnique(out, builtIn.trimmed());
    if (windows) {
        static const std::array<QString, 6> names{u"libtesseract-5.dll"_s, u"tesseract55.dll"_s,
                                                  u"tesseract54.dll"_s,    u"tesseract53.dll"_s,
                                                  u"libtesseract-4.dll"_s, u"tesseract41.dll"_s};
        for (const QString &dir : windowsDirs) {
            if (dir.isEmpty())
                continue;
            for (const QString &name : names)
                addUnique(out, QDir::cleanPath(dir + u'/' + name));
        }
        for (const QString &name : names)
            addUnique(out, name);
    } else {
        addUnique(out, u"libtesseract.so.5"_s);
        addUnique(out, u"libtesseract.so.4"_s);
    }
    return out;
}

QStringList tessdataCandidates(const QString &userDir, const QStringList &envDirs, const QStringList &builtIn,
                               const QString &libraryDir, bool windows)
{
    QStringList out;
    const auto add = [&out](const QString &dir) {
        if (!dir.trimmed().isEmpty())
            addUnique(out, QDir::cleanPath(dir.trimmed()));
    };
    add(userDir);
    for (const QString &dir : envDirs)
        add(dir);
    for (const QString &dir : builtIn)
        add(dir);
    if (!libraryDir.isEmpty()) {
        add(libraryDir + u"/../share/tessdata"_s);
        add(libraryDir + u"/tessdata"_s);
    }
    if (!windows) {
        for (const QString &dir :
             {u"/usr/share/tesseract-ocr/5/tessdata"_s, u"/usr/share/tesseract-ocr/4.00/tessdata"_s,
              u"/usr/share/tessdata"_s, u"/usr/share/tesseract/tessdata"_s, u"/usr/local/share/tessdata"_s})
            add(dir);
    }
    return out;
}

QStringList tessdataLanguages(const QStringList &files)
{
    static const QRegularExpression code(u"^[A-Za-z0-9_-]{2,32}$"_s);
    QStringList out;
    for (const QString &file : files) {
        if (!file.endsWith(u".traineddata"_s))
            continue;
        const QString name = file.chopped(12);
        if (name == u"osd"_s || name == u"equ"_s || !code.match(name).hasMatch())
            continue;
        addUnique(out, name);
    }
    std::ranges::sort(out);
    return out;
}

QString tesseractCode(const QString &language)
{
    static const QHash<QString, QString> codes{
            {u"pl"_s, u"pol"_s}, {u"en"_s, u"eng"_s}, {u"de"_s, u"deu"_s}, {u"fr"_s, u"fra"_s}, {u"es"_s, u"spa"_s},
            {u"it"_s, u"ita"_s}, {u"pt"_s, u"por"_s}, {u"nl"_s, u"nld"_s}, {u"cs"_s, u"ces"_s}, {u"sk"_s, u"slk"_s},
            {u"uk"_s, u"ukr"_s}, {u"ru"_s, u"rus"_s}, {u"lt"_s, u"lit"_s}, {u"lv"_s, u"lav"_s}, {u"hu"_s, u"hun"_s},
            {u"sv"_s, u"swe"_s}, {u"da"_s, u"dan"_s}, {u"nb"_s, u"nor"_s}, {u"no"_s, u"nor"_s}, {u"fi"_s, u"fin"_s},
            {u"be"_s, u"bel"_s}, {u"ro"_s, u"ron"_s}, {u"el"_s, u"ell"_s}, {u"tr"_s, u"tur"_s},
    };
    QString primary = language.trimmed().toLower();
    const qsizetype cut = primary.indexOf(QRegularExpression(u"[-_]"_s));
    if (cut > 0)
        primary.truncate(cut);
    return codes.value(primary);
}

QString ocrLanguagesFor(const QString &setting, const QString &uiLanguage, const QStringList &installed)
{
    QStringList wanted;
    const QString trimmed = setting.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(u"auto"_s, Qt::CaseInsensitive) == 0) {
        addUnique(wanted, tesseractCode(uiLanguage));
        addUnique(wanted, u"eng"_s);
    } else {
        static const QRegularExpression separators(u"[,+\\s]+"_s);
        for (const QString &part : trimmed.split(separators, Qt::SkipEmptyParts)) {
            const QString lower = part.toLower();
            addUnique(wanted, installed.contains(part)    ? part
                              : installed.contains(lower) ? lower
                                                          : tesseractCode(part));
        }
    }
    QStringList out;
    for (const QString &code : std::as_const(wanted)) {
        if (installed.contains(code))
            addUnique(out, code);
    }
    // "auto" with neither the UI language's model nor English: the first model there is.
    if (out.isEmpty() && (trimmed.isEmpty() || trimmed.compare(u"auto"_s, Qt::CaseInsensitive) == 0)
        && !installed.isEmpty())
        out.append(installed.constFirst());
    return out.join(u'+');
}

} // namespace brulion::platform
