// Importers of Markdown trees: plain Markdown / text folders, Obsidian vaults and Bear exports
// (TextBundles).
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "util/paths.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

bool isTextPage(const QString &path)
{
    const QString ext = paths::extension(path);
    return ext == u".md" || ext == u".markdown" || ext == u".txt";
}

// --- Markdown ------------------------------------------------------------------------------------

class MarkdownImporter final : public Importer {
public:
    QString id() const override { return u"markdown"_s; }
    QString name() const override { return u"Markdown"_s; }
    QStringList fileTypes() const override { return {u"*.md"_s, u"*.markdown"_s, u"*.txt"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override { return anyPath(files, isTextPage) ? 20 : 0; }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        TreeImport tree;
        tree.isPage = isTextPage;
        importTree(stripCommonRoot(files), ctx, tree);
        return ctx.take();
    }
};

// --- Obsidian ------------------------------------------------------------------------------------

/// Folder containing ".obsidian/" (shortest), or nullopt.
std::optional<QString> obsidianRoot(const QList<InputFile> &files)
{
    std::optional<QString> best;
    for (const InputFile &f : files) {
        const QStringList segments = f.path.split(u'/');
        const qsizetype k = segments.indexOf(u".obsidian"_s);
        if (k < 0)
            continue;
        const QString root = segments.mid(0, k).join(u'/');
        if (!best || root.size() < best->size())
            best = root;
    }
    return best;
}

class ObsidianImporter final : public Importer {
public:
    QString id() const override { return u"obsidian"_s; }
    QString name() const override { return u"Obsidian"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s, u"*.md"_s}; }

    int detect(const QList<InputFile> &files) const override { return obsidianRoot(files) ? 95 : 0; }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QList<InputFile> vault;
        if (const auto root = obsidianRoot(files); root && !root->isEmpty()) {
            const QString prefix = *root + u'/';
            for (const InputFile &f : files) {
                if (f.path.startsWith(prefix))
                    vault.append(InputFile{f.path.sliced(prefix.size()), f.data});
            }
        } else {
            vault = files;
        }
        importTree(vault, ctx, TreeImport());
        return ctx.take();
    }
};

// --- Bear ----------------------------------------------------------------------------------------

/// "Folder/Note.textbundle" for a path inside a TextBundle, else empty.
QString bundleOf(const QString &path)
{
    static const QRegularExpression re(u"^(.*?\\.textbundle)(?:/|$)"_s, QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(path);
    return m.hasMatch() ? m.captured(1) : QString();
}

bool isBundleText(const QString &path)
{
    const QString bundle = bundleOf(path);
    if (bundle.isEmpty())
        return false;
    const QString rest = path.sliced(bundle.size() + 1).toLower();
    return rest == u"text.md" || rest == u"text.markdown" || rest == u"text.txt";
}

/// Bear syntax -> Brulion: `#multi word tags#` -> `#[[multi word tags]]`, `::highlight::` ->
/// `==highlight==` (outside code).
QString convertBear(const QString &content)
{
    static const QRegularExpression multiTag(
            u"(?<![^\\s(])#([^\\s#\\[\\]][^#\\n\\[\\]]*?\\s[^#\\n\\[\\]]*?[^\\s#\\[\\]])#(?![\\p{L}\\p{N}_#])"_s);
    static const QRegularExpression highlight(u"::(?=\\S)([^:\\n]+?)(?<=\\S)::"_s);
    return mapLinesOutsideCode(content, [](const QString &line, const QString &masked) -> QString {
        struct Hit {
            qsizetype from;
            qsizetype to;
            QString text;
        };
        QList<Hit> hits;
        for (auto it = multiTag.globalMatch(masked); it.hasNext();) {
            const auto m = it.next();
            hits.append(Hit{m.capturedStart(), m.capturedEnd(),
                            u"#[["_s + line.sliced(m.capturedStart(1), m.capturedLength(1)).trimmed() + u"]]"_s});
        }
        for (auto it = highlight.globalMatch(masked); it.hasNext();) {
            const auto m = it.next();
            hits.append(Hit{m.capturedStart(), m.capturedEnd(),
                            u"=="_s + line.sliced(m.capturedStart(1), m.capturedLength(1)) + u"=="_s});
        }
        if (hits.isEmpty())
            return line;
        std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.from < b.from; });
        QString out;
        qsizetype last = 0;
        for (const Hit &h : std::as_const(hits)) {
            if (h.from < last)
                continue;
            out += QStringView(line).sliced(last, h.from - last);
            out += h.text;
            last = h.to;
        }
        return out + QStringView(line).sliced(last);
    });
}

/// The info.json files of the TextBundles, by bundle folder (case-folded).
QHash<QString, const InputFile *> bundleInfoFiles(const QList<InputFile> &files)
{
    QHash<QString, const InputFile *> out;
    for (const InputFile &f : files) {
        const QString bundle = bundleOf(f.path);
        if (!bundle.isEmpty() && f.path.compare(bundle + u"/info.json"_s, Qt::CaseInsensitive) == 0)
            out.insert(bundle.toCaseFolded(), &f);
    }
    return out;
}

/// created / updated from a TextBundle's info.json (Bear's "net.shinyfrog.bear" section).
QVariantMap bundleInfo(const QHash<QString, const InputFile *> &infoFiles, const QString &bundle, const QTimeZone &zone)
{
    QVariantMap out;
    const InputFile *f = infoFiles.value(bundle.toCaseFolded());
    if (!f)
        return out;
    const QJsonObject root = QJsonDocument::fromJson(f->data).object();
    const QJsonObject bear = root.value(u"net.shinyfrog.bear"_s).toObject();
    const auto stamp = [&](const QString &key) {
        return wallClock(QDateTime::fromString(bear.value(key).toString(), Qt::ISODateWithMs), zone);
    };
    if (const QString c = stamp(u"creationDate"_s); !c.isEmpty())
        out.insert(u"created"_s, c);
    if (const QString m = stamp(u"modificationDate"_s); !m.isEmpty())
        out.insert(u"updated"_s, m);
    return out;
}

class BearImporter final : public Importer {
public:
    QString id() const override { return u"bear"_s; }
    QString name() const override { return u"Bear"_s; }
    QStringList fileTypes() const override { return {u"*.bear2bk"_s, u"*.textpack"_s, u"*.zip"_s, u"*.md"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        return anyPath(files, [](const QString &p) { return !bundleOf(p).isEmpty(); }) ? 90 : 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const QList<InputFile> input = stripCommonRoot(
                files, [](const QString &folder) { return folder.endsWith(u".textbundle"_s, Qt::CaseInsensitive); });
        TreeImport tree;
        tree.isPage = [](const QString &p) { return bundleOf(p).isEmpty() ? isTextPage(p) : isBundleText(p); };
        tree.skip = [](const QString &p) {
            const QString bundle = bundleOf(p);
            return !bundle.isEmpty() && p.compare(bundle + u"/info.json"_s, Qt::CaseInsensitive) == 0;
        };
        tree.pageRelative = [](const QString &p) -> QString {
            const QString bundle = bundleOf(p);
            if (bundle.isEmpty()) {
                QString stem = paths::stripMarkdownExtension(p);
                if (stem.endsWith(u".txt"_s, Qt::CaseInsensitive))
                    stem.chop(4);
                return stem;
            }
            return bundle.first(bundle.size() - QStringLiteral(".textbundle").size());
        };
        tree.attachmentRelative = [](const QString &p) -> QString {
            const QString bundle = bundleOf(p);
            if (bundle.isEmpty())
                return p;
            // "Note.textbundle/assets/x.png" -> "Note/x.png"
            QString rest = p.sliced(bundle.size() + 1);
            if (rest.startsWith(u"assets/"_s, Qt::CaseInsensitive))
                rest = rest.sliced(7);
            return paths::basename(bundle.first(bundle.size() - 11)) + u'/' + rest;
        };
        const QTimeZone zone = options.timeZone;
        const QHash<QString, const InputFile *> infoFiles = bundleInfoFiles(input);
        tree.convert = [&](const QString &content, const QString &source) -> QString {
            const QString body = convertBear(content);
            const QString bundle = bundleOf(source);
            const QVariantMap info = bundle.isEmpty() ? QVariantMap() : bundleInfo(infoFiles, bundle, zone);
            if (info.isEmpty())
                return body;
            return noteWithFrontmatter(info, {u"created"_s, u"updated"_s}, body);
        };
        importTree(input, ctx, tree);
        return ctx.take();
    }
};

} // namespace

std::unique_ptr<Importer> makeMarkdownImporter()
{
    return std::make_unique<MarkdownImporter>();
}

std::unique_ptr<Importer> makeObsidianImporter()
{
    return std::make_unique<ObsidianImporter>();
}

std::unique_ptr<Importer> makeBearImporter()
{
    return std::make_unique<BearImporter>();
}

} // namespace brulion::io::detail
