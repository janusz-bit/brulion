#include "pagetools.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "io/exporter.h"
#include "markdown/inline.h"
#include "markdown/rename.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspace.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

QString withoutMd(const QString &title)
{
    return title.endsWith(u".md"_s, Qt::CaseInsensitive) ? title.left(title.size() - 3) : title;
}

/// The tag that names the page at `path` (docs/SYNTAX.md §3): its key and how it is shown —
/// the title, or the path when the title names another page (a duplicate base name); when that
/// tag is not in use but another one naming the page is (an alias: #Janek), the most used of those.
/// A page not written yet: the tag in use that names it most exactly (#praca/raporty for
/// praca/raporty.md rather than #raporty). Nothing for an attachment: tags name notes only.
struct PageTag {
    QString key;
    QString display;
};

PageTag pageTag(const VaultIndex *idx, const QString &path)
{
    if (!paths::isMarkdown(path))
        return {};
    const QString title = paths::pageTitle(path);
    PageTag tag{title.normalized(QString::NormalizationForm_C).toLower(), title};
    if (!idx)
        return tag;
    if (const auto named = idx->resolveTag(tag.key); named && *named != path) {
        tag.display = paths::stripMarkdownExtension(path);
        tag.key = tag.display.normalized(QString::NormalizationForm_C).toLower();
    }
    const QStringList naming = idx->tagsNaming(path);
    if (!idx->note(path)) {
        QString exact;
        for (const QString &key : naming) {
            const qsizetype depth = key.count(u'/');
            const qsizetype best = exact.count(u'/');
            if (exact.isEmpty() || depth > best || (depth == best && key.size() > exact.size()))
                exact = key;
        }
        if (!exact.isEmpty())
            tag = {exact, exact};
    }
    PageTag other;
    for (const TagCount &t : idx->tags()) { // most used first
        if (t.tag == tag.key) {
            if (!t.display.isEmpty())
                tag.display = t.display;
            return tag;
        }
        if (other.key.isEmpty() && naming.contains(t.tag))
            other = {t.tag, t.display.isEmpty() ? t.tag : t.display};
    }
    return other.key.isEmpty() ? tag : other;
}

QString propertyText(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QStringList>())
        return value.toStringList().join(u", "_s);
    if (value.metaType() == QMetaType::fromType<QVariantList>()) {
        QStringList parts;
        for (const QVariant &v : value.toList())
            parts.append(v.toString());
        return parts.join(u", "_s);
    }
    return value.toString();
}

} // namespace

PageTools::PageTools(QObject *parent) : QObject(parent) { }

QObject *PageTools::workspaceObject() const
{
    return m_workspace.data();
}

void PageTools::setWorkspaceObject(QObject *workspace)
{
    setWorkspace(qobject_cast<shell::Workspace *>(workspace));
}

void PageTools::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    m_workspace = workspace;
    Q_EMIT workspaceChanged();
}

QVariantMap PageTools::renameImpact(const QString &rawPath) const
{
    int links = 0;
    int tags = 0;
    QSet<QString> notes;
    const VaultIndex *idx = m_workspace ? m_workspace->index() : nullptr;
    const QString path = paths::normalize(rawPath);
    if (idx) {
        m_workspace->finishIndexing(); // every linking file must be known
        for (const VaultPath &source : idx->linksTo(path)) {
            if (source == path)
                continue;
            int here = 0;
            for (const ResolvedLink &l : idx->outgoingLinks(source)) {
                if (l.target == path && l.link.kind != LinkKind::BlockRef && l.link.kind != LinkKind::BlockEmbed)
                    ++here;
            }
            if (here > 0) {
                notes.insert(source);
                links += here;
            }
        }
        // The tags that name the page (docs/SYNTAX.md §3) are renamed with it, as
        // Workspace::renamePage() does: only those (not another page's of the same name).
        const QString title = paths::pageTitle(path);
        const QStringList keys = idx->tagsNaming(path);
        const auto naming = [&keys](const QString &key) { return keys.contains(key); };
        const QString placeholder = title + u" (2)"_s; // any other name: what changes is the same
        for (const VaultPath &source : idx->tagsTo(path)) {
            if (source == path)
                continue;
            const qsizetype here =
                    markdown::tagsToRename(idx->content(source), title, placeholder, path, {}, naming).size();
            if (here > 0) {
                notes.insert(source);
                tags += int(here);
            }
        }
    }
    return {{u"notes"_s, int(notes.size())}, {u"links"_s, links}, {u"tags"_s, tags}};
}

QString PageTools::tagFor(const QString &rawPath) const
{
    const VaultIndex *idx = m_workspace ? m_workspace->index() : nullptr;
    const PageTag tag = pageTag(idx, paths::normalize(rawPath));
    return tag.key.isEmpty() ? QString() : markdown::tagSyntax(tag.display);
}

QString PageTools::validateTitle(const QString &rawPath, const QString &rawTitle) const
{
    const QString title = withoutMd(rawTitle).trimmed();
    if (title.isEmpty())
        return tr("Enter a title.");
    const QString name = paths::sanitizeFileName(title);
    if (name.trimmed().isEmpty())
        return tr("The title has no characters a file name can use.");
    if (!m_workspace)
        return {};
    const QString path = paths::normalize(rawPath);
    const QString folder = paths::dirname(path);
    const QString target = folder.isEmpty() ? name + u".md"_s : paths::join(folder, name + u".md"_s);
    if (target.compare(path, Qt::CaseInsensitive) != 0 && m_workspace->exists(target))
        return tr("A page named “%1” already exists in this folder.").arg(name);
    // Names differing only in case ("Dom" next to "dom") are one file on Android's shared
    // storage and in other case-insensitive folders a synced vault may land in; links do not
    // tell them apart either.
    if (const VaultIndex *idx = m_workspace->index()) {
        for (const VaultPath &other : idx->paths()) {
            if (other.compare(path, Qt::CaseInsensitive) != 0 && other.compare(target, Qt::CaseInsensitive) == 0)
                return tr("A page named “%1” already exists in this folder.").arg(paths::pageTitle(other));
        }
    }
    return {};
}

QVariantMap PageTools::summary(const QString &rawPath) const
{
    const QString path = paths::normalize(rawPath);
    QVariantMap out{ { u"exists"_s, false },
                     { u"title"_s, paths::pageTitle(path) },
                     { u"folder"_s, paths::dirname(path) },
                     { u"journalDate"_s, QString() },
                     { u"aliases"_s, QStringList() },
                     { u"tags"_s, QVariantList() },
                     { u"properties"_s, QVariantList() },
                     { u"words"_s, 0 },
                     { u"backlinks"_s, 0 } };
    QVariantList parts;
    const QString folder = paths::dirname(path);
    if (!folder.isEmpty()) {
        const QStringList segments = folder.split(u'/');
        for (qsizetype i = 0; i < segments.size(); ++i)
            parts.append(QVariantMap{ { u"title"_s, segments.at(i) }, { u"path"_s, segments.mid(0, i + 1).join(u'/') } });
    }
    out.insert(u"folderParts"_s, parts);
    if (!m_workspace)
        return out;
    out.insert(u"exists"_s, m_workspace->exists(path));
    const VaultIndex *idx = m_workspace->index();
    const ParsedNote *note = idx ? idx->note(path) : nullptr;
    if (!note) {
        // A page not written yet (a link's or a tag's): what already names it.
        if (idx && !path.isEmpty()) {
            const QList<TagReference> tagged = idx->tagReferences(path);
            QSet<QString> linking;
            for (const Backlink &b : idx->backlinks(path))
                linking.insert(b.path);
            for (const TagReference &r : tagged)
                linking.insert(r.path);
            out.insert(u"backlinks"_s, int(linking.size()));
            out.insert(u"tag"_s, tagInfo(idx, path, int(tagged.size())));
        }
        return out;
    }
    if (note->journalDate)
        out.insert(u"journalDate"_s, isoDate(*note->journalDate));
    out.insert(u"aliases"_s, note->aliases);

    QHash<QString, QString> display;
    for (const TagCount &t : idx->tags())
        display.insert(t.tag, t.display.isEmpty() ? t.tag : t.display);
    QVariantList tags;
    for (const QString &t : pageTags(*note))
        tags.append(QVariantMap{ { u"tag"_s, t }, { u"display"_s, display.value(t, t) } });
    out.insert(u"tags"_s, tags);

    static const QSet<QString> shown{ u"tags"_s, u"tag"_s, u"aliases"_s, u"alias"_s, u"title"_s, u"id"_s,
                                      u"collapsed"_s, u"public"_s, u"filters"_s };
    QVariantList props;
    QStringList keys = note->properties.keys();
    for (auto it = note->frontmatter.cbegin(); it != note->frontmatter.cend(); ++it) {
        if (!keys.contains(it.key()))
            keys.append(it.key());
    }
    std::sort(keys.begin(), keys.end());
    for (const QString &k : std::as_const(keys)) {
        if (shown.contains(k.toLower()))
            continue;
        const QVariant v = note->properties.contains(k) ? note->properties.value(k) : note->frontmatter.value(k);
        const QString text = propertyText(v).trimmed();
        if (!text.isEmpty())
            props.append(QVariantMap{ { u"key"_s, k }, { u"value"_s, oneLine(text, 80) } });
    }
    out.insert(u"properties"_s, props);

    int words = 0;
    static const QRegularExpression word(u"[\\p{L}\\p{N}]+"_s);
    for (const Block &b : note->blocks) {
        auto it = word.globalMatch(b.text);
        while (it.hasNext()) {
            it.next();
            ++words;
        }
    }
    out.insert(u"words"_s, words);
    // Pages linking here or carrying a tag that names this page.
    QSet<QString> linking;
    for (const Backlink &b : idx->backlinks(path))
        linking.insert(b.path);
    const QList<TagReference> tagged = idx->tagReferences(path);
    for (const TagReference &r : tagged)
        linking.insert(r.path);
    out.insert(u"backlinks"_s, int(linking.size()));
    out.insert(u"tag"_s, tagInfo(idx, path, int(tagged.size())));
    return out;
}

QVariantMap PageTools::tagInfo(const VaultIndex *idx, const QString &path, int references)
{
    const PageTag tag = pageTag(idx, path);
    QVariantList subTags;
    const QString prefix = tag.key + u'/';
    if (idx && !tag.key.isEmpty()) {
        for (const TagCount &t : idx->tags()) {
            if (t.tag.startsWith(prefix))
                subTags.append(QVariantMap{{u"tag"_s, t.tag},
                                           {u"display"_s, t.display.isEmpty() ? t.tag : t.display},
                                           {u"count"_s, t.count}});
        }
    }
    return {{u"key"_s, tag.key},
            {u"display"_s, tag.display},
            {u"text"_s, tag.key.isEmpty() ? QString() : markdown::tagSyntax(tag.display)},
            {u"references"_s, references},
            {u"subTags"_s, subTags}};
}

QString PageTools::wikiLink(const QString &path) const
{
    return wikiLinkFor(m_workspace ? m_workspace->index() : nullptr, paths::normalize(path));
}

bool PageTools::openLink(const QString &url, const QString &fromPath, bool newPane) const
{
    const qsizetype colon = url.indexOf(u':');
    if (colon <= 0 || !m_workspace)
        return false;
    const QString scheme = url.left(colon);
    const QString rest = url.mid(colon + 1);
    if (scheme == u"ext") {
        m_workspace->openUrl(QUrl(rest));
        return true;
    }
    if (scheme == u"tag") {
        m_workspace->navigate({ { u"view"_s, u"tag"_s }, { u"tag"_s, QUrl::fromPercentEncoding(rest.toUtf8()) } }, newPane);
        return true;
    }
    if (scheme == u"block") {
        m_workspace->navigate({ { u"view"_s, u"page"_s }, { u"blockId"_s, rest } }, newPane);
        return true;
    }
    if (scheme != u"page")
        return false;
    QString target = rest;
    QString fragment;
    const qsizetype hash = rest.indexOf(u'#');
    if (hash >= 0) {
        target = rest.left(hash);
        fragment = QUrl::fromPercentEncoding(rest.mid(hash + 1).toUtf8());
    }
    target = QUrl::fromPercentEncoding(target.toUtf8());
    QVariantMap route{ { u"view"_s, u"page"_s } };
    if (target.isEmpty()) {
        route.insert(u"path"_s, paths::normalize(fromPath));
    } else {
        const QString resolved = m_workspace->resolveLink(target, paths::normalize(fromPath));
        if (!resolved.isEmpty())
            route.insert(u"path"_s, resolved);
        route.insert(u"target"_s, target);
    }
    if (fragment.startsWith(u'^'))
        route.insert(u"blockId"_s, fragment.mid(1));
    else if (!fragment.isEmpty())
        route.insert(u"heading"_s, fragment);
    m_workspace->navigate(route, newPane);
    return true;
}

QString PageTools::journalPathFor(const QString &date) const
{
    const QDate d = parseIsoDate(date);
    if (!d.isValid() || !m_workspace)
        return {};
    const QString preferred = m_workspace->journalPath(date);
    if (m_workspace->exists(preferred))
        return preferred;
    if (const VaultIndex *idx = m_workspace->index()) {
        for (const JournalRef &j : idx->journals()) {
            if (j.date == d)
                return j.path;
        }
    }
    return preferred;
}

bool PageTools::copyText(const QString &text) const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(text);
    return true;
}

QVariantList PageTools::exportFormats() const
{
    return { QVariantMap{ { u"id"_s, u"markdown"_s }, { u"name"_s, tr("Markdown") }, { u"extension"_s, u".md"_s } },
             QVariantMap{ { u"id"_s, u"html"_s }, { u"name"_s, tr("HTML document") }, { u"extension"_s, u".html"_s } } };
}

QString PageTools::exportFileName(const QString &path, const QString &format) const
{
    const QString base = paths::sanitizeFileName(paths::pageTitle(path));
    return base + (format == u"html" ? u".html"_s : u".md"_s);
}

QByteArray PageTools::exportData(const QString &rawPath, const QString &format) const
{
    if (!m_workspace || !m_workspace->vault())
        return {};
    const QString path = paths::normalize(rawPath);
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return {};
    if (format != u"html")
        return content.toUtf8();
    const io::Exporter *exporter = io::exporterById(u"html-single");
    if (!exporter)
        return {};
    io::ExportInput input;
    input.files.append(io::FileEntry{ path, content.toUtf8() });
    // Embedded pictures and files go into the document (data: URLs).
    if (const VaultIndex *idx = m_workspace->index()) {
        QSet<QString> added;
        for (const ResolvedLink &l : idx->outgoingLinks(path)) {
            if (l.link.kind != LinkKind::Embed || l.target.isEmpty() || paths::isMarkdown(l.target) || added.contains(l.target))
                continue;
            added.insert(l.target);
            bool read = false;
            const QByteArray bytes = m_workspace->vault()->readBinary(l.target, &read);
            if (read)
                input.files.append(io::FileEntry{ l.target, bytes });
        }
    }
    input.config = m_workspace->vault()->config();
    input.vaultName = paths::pageTitle(path);
    input.now = m_workspace->now();
    const io::ExportResult result = io::runExport(*exporter, input);
    return result.files.isEmpty() ? QByteArray() : result.files.first().data;
}

QString PageTools::exportText(const QString &path, const QString &format) const
{
    return QString::fromUtf8(exportData(path, format));
}

bool PageTools::copyExport(const QString &path, const QString &format) const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || !m_workspace)
        return false;
    const QByteArray data = exportData(path, format);
    if (data.isEmpty() && !m_workspace->exists(path))
        return false;
    if (format == u"html") {
        auto *mime = new QMimeData;
        mime->setHtml(QString::fromUtf8(data));
        mime->setText(m_workspace->readFile(path));
        clipboard->setMimeData(mime);
    } else {
        clipboard->setText(QString::fromUtf8(data));
    }
    return true;
}

bool PageTools::saveExport(const QString &path, const QString &format, const QUrl &target) const
{
    if (!m_workspace || !target.isValid() || target.isEmpty())
        return false;
    const QByteArray data = exportData(path, format);
    if (data.isEmpty() && !m_workspace->exists(path))
        return false;
    QFile file(target.isLocalFile() ? target.toLocalFile() : target.toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool written = file.write(data) == data.size();
    file.close();
    return written;
}

} // namespace brulion::content
