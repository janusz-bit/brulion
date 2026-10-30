// brulion-uimetrics — the UI benchmark's metric tool (uimetrics.h; docs/UI.md "UI benchmark
// (Brulion vs Obsidian)"):
//
//   brulion-uimetrics measure   --png X.png --dump X.layout.json --device desktop|phone
//                               [--screen id] [--size name] [--theme name] --out m.json
//   brulion-uimetrics summarize --dir <out>/metrics --out <out>/metrics.json
//   brulion-uimetrics check-manifest tests/app/uibench/screens.json
//   brulion-uimetrics lint      --png X.png --dump X.layout.json --device desktop|phone
//                               [--screen id] [--allow lint-allow.json]
//
// Exit code 0 = ok, 1 = lint failures, 2 = bad input (the message on stderr names the file).
#include "uimetrics.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace brulion::uibench;

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: brulion-uimetrics measure --png X.png --dump X.layout.json --device desktop|phone "
                 "[--screen id] [--size name] [--theme name] --out m.json\n"
                 "       brulion-uimetrics summarize --dir <metrics dir> --out metrics.json\n"
                 "       brulion-uimetrics check-manifest screens.json\n"
                 "       brulion-uimetrics lint --png X.png --dump X.layout.json --device desktop|phone "
                 "[--screen id] [--allow lint-allow.json]\n");
    return 2;
}

int bad(const QString &message)
{
    std::fprintf(stderr, "brulion-uimetrics: %s\n", qPrintable(message));
    return 2;
}

QString option(const QStringList &args, const QString &name)
{
    const qsizetype i = args.indexOf(name);
    return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

bool readFile(const QString &path, QByteArray *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = path + u": "_s + file.errorString();
        return false;
    }
    *out = file.readAll();
    return true;
}

bool writeJson(const QString &path, const QJsonObject &json, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = path + u": "_s + file.errorString();
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = path + u": "_s + file.errorString();
        return false;
    }
    return true;
}

/// The shot: the PNG and its dump, checked against each other.
struct Shot {
    LayoutDump dump;
    QImage png;
    Device device = Device::Desktop;
};

bool loadShot(const QStringList &args, Shot *shot, QString *error)
{
    const QString pngPath = option(args, u"--png"_s), dumpPath = option(args, u"--dump"_s);
    const QString device = option(args, u"--device"_s);
    if (pngPath.isEmpty() || dumpPath.isEmpty() || (device != u"desktop" && device != u"phone")) {
        *error = u"--png, --dump and --device desktop|phone are required"_s;
        return false;
    }
    shot->device = device == u"phone" ? Device::Phone : Device::Desktop;
    QByteArray json;
    if (!readFile(dumpPath, &json, error))
        return false;
    QString parseError;
    const auto dump = parseDump(json, &parseError);
    if (!dump) {
        *error = dumpPath + u": "_s + parseError;
        return false;
    }
    if (dump->texts.isEmpty() && dump->targets.isEmpty() && dump->boxes.isEmpty()) {
        *error = dumpPath + u": empty dump"_s;
        return false;
    }
    shot->dump = *dump;
    if (!shot->png.load(pngPath)) {
        *error = pngPath + u": not a readable image"_s;
        return false;
    }
    const QSize expected(int(std::lround(dump->viewport.width() * dump->dpr)),
                         int(std::lround(dump->viewport.height() * dump->dpr)));
    if (std::abs(shot->png.width() - expected.width()) > 1 || std::abs(shot->png.height() - expected.height()) > 1) {
        *error = u"%1: %2×%3 px, but the dump's viewport × dpr is %4×%5"_s.arg(pngPath)
                     .arg(shot->png.width()).arg(shot->png.height()).arg(expected.width()).arg(expected.height());
        return false;
    }
    return true;
}

int measure(const QStringList &args)
{
    const QString out = option(args, u"--out"_s);
    if (out.isEmpty())
        return usage();
    Shot shot;
    QString error;
    if (!loadShot(args, &shot, &error))
        return bad(error);
    const QJsonObject meta{
        { u"size"_s, option(args, u"--size"_s) },
        { u"theme"_s, option(args, u"--theme"_s) },
        { u"png"_s, QFileInfo(option(args, u"--png"_s)).fileName() },
        { u"dump"_s, QFileInfo(option(args, u"--dump"_s)).fileName() },
    };
    const QJsonObject result = measureJson(shot.dump, shot.png, shot.device, option(args, u"--screen"_s), meta);
    if (!writeJson(out, result, &error))
        return bad(error);
    return 0;
}

int summarizeCommand(const QStringList &args)
{
    const QString dir = option(args, u"--dir"_s), out = option(args, u"--out"_s);
    if (dir.isEmpty() || out.isEmpty())
        return usage();
    QList<QJsonObject> measurements;
    QStringList files;
    QDirIterator it(dir, { u"*.json"_s }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    files.sort();
    for (const QString &path : std::as_const(files)) {
        QByteArray json;
        QString error;
        if (!readFile(path, &json, &error))
            return bad(error);
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        if (!doc.isObject())
            return bad(path + u": not a JSON object"_s);
        measurements.append(doc.object());
    }
    if (measurements.isEmpty())
        return bad(dir + u": no measurements"_s);
    QString error;
    if (!writeJson(out, summarize(measurements), &error))
        return bad(error);
    return 0;
}

int checkManifestCommand(const QStringList &args)
{
    if (args.size() < 3)
        return usage();
    QByteArray json;
    QString error;
    if (!readFile(args.at(2), &json, &error))
        return bad(error);
    const QStringList errors = checkManifest(json);
    for (const QString &e : errors)
        std::fprintf(stderr, "%s: %s\n", qPrintable(args.at(2)), qPrintable(e));
    return errors.isEmpty() ? 0 : 2;
}

int lintCommand(const QStringList &args)
{
    Shot shot;
    QString error;
    if (!loadShot(args, &shot, &error))
        return bad(error);
    QByteArray allow;
    const QString allowPath = option(args, u"--allow"_s);
    if (!allowPath.isEmpty() && !readFile(allowPath, &allow, &error))
        return bad(error);
    const QList<LintFailure> failures =
        lint(shot.dump, shot.png, shot.device, option(args, u"--screen"_s), allow, &error);
    if (!error.isEmpty())
        return bad(allowPath + u": "_s + error);
    for (const LintFailure &f : failures)
        std::fprintf(stdout, "%s: %s %s \"%s\": %s\n", qPrintable(option(args, u"--png"_s)), qPrintable(f.kind),
                     qPrintable(f.type), qPrintable(f.text), qPrintable(f.detail));
    return failures.isEmpty() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return usage();
    const QString command = args.at(1);
    if (command == u"measure")
        return measure(args);
    if (command == u"summarize")
        return summarizeCommand(args);
    if (command == u"check-manifest")
        return checkManifestCommand(args);
    if (command == u"lint")
        return lintCommand(args);
    return usage();
}
