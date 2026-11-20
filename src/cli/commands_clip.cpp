// The Web Clipper's commands (docs/CLIPPER.md, docs/CLI.md "Commands"): `clip save` — a page's HTML
// (an article, a selection, the whole page), a screenshot, a picture or a link as a new page of the
// notebook, a block of today's journal or of a heading's section, filled into a template, its
// pictures saved as attachments —, `clip host` (the native messaging host, cliphost.cpp) and
// `clip install` / `uninstall` / `status` / `manifest` (clipinstall.cpp).
#include "clip_p.h"
#include "commands_p.h"

#include "clipper/article.h"
#include "clipper/cliptemplate.h"
#include "clipper/pictures.h"
#include "io/htmlmarkdown.h"
#include "markdown/journal.h"
#include "notebook/journals.h"
#include "notebook/pages.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "webpicture/webpicture.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::cli {

using namespace brulion::clipper;

namespace {

/// The longest file name of a clipped page (characters, without ".md"): a Polish title of this
/// length stays below the 255 bytes file systems allow.
constexpr qsizetype MaxFileNameChars = 100;
/// The longest Markdown a preview shows.
constexpr qsizetype MaxPreviewChars = 200 * 1024;

QString hostOf(const QString &url)
{
    QString host = QUrl(url).host();
    if (host.startsWith(u"www."_s))
        host.remove(0, 4);
    return host;
}

/// A text cut at `max` characters, never inside a surrogate pair.
QString cutText(const QString &text, qsizetype max)
{
    if (text.size() <= max)
        return text;
    return text.left(max > 0 && text.at(max - 1).isHighSurrogate() ? max - 1 : max);
}

/// The title of a clip the page gave none: a link's text, the address's host and path, else
/// "Wycinek 2026-11-20 18-30" (in the notebook's language).
QString fallbackTitle(const ClipRequest &r, const QDateTime &now, dates::UiLocale locale)
{
    if (r.kind == ClipKind::Link && !r.linkText.isEmpty())
        return r.linkText;
    const QUrl url(r.kind == ClipKind::Link ? r.linkUrl : r.url);
    if (!url.host().isEmpty()) {
        QString path = url.path(QUrl::FullyDecoded);
        while (path.endsWith(u'/'))
            path.chop(1);
        return cleanLine(QString(hostOf(url.toString()) + path), MaxTitleChars);
    }
    if (url.isLocalFile())
        return cleanLine(QFileInfo(url.toLocalFile()).completeBaseName(), MaxTitleChars);
    const QString stamp = now.toString(u"yyyy-MM-dd HH-mm"_s);
    return (locale == dates::UiLocale::Polish ? u"Wycinek "_s : u"Clip "_s) + stamp;
}

/// Plain text as Markdown lines (escaped).
QString textAsMarkdown(const QString &text)
{
    QStringList lines = text.trimmed().split(u'\n');
    for (QString &line : lines) {
        line.remove(u'\r');
        line = io::escapeMarkdownText(line.trimmed(), true);
    }
    return lines.join(u'\n');
}

struct Built {
    QString title;
    QString text; ///< the filled template, picture tokens in it
    PictureList pictures;
    QString templateId;
};

std::optional<QString> templateText(Call &call, const ClipRequest &r, QString *id)
{
    if (!r.templ.text.isEmpty()) {
        *id = u"text"_s;
        return r.templ.text;
    }
    if (!r.templ.page.isEmpty()) {
        const auto path = call.vaultPath(r.templ.page);
        if (!path)
            return std::nullopt;
        const notebook::Notebook::Current current = call.nb()->read(*path);
        if (!current.exists) {
            call.setError(ExitNotFound, u"template_not_found"_s, tr("No template page %1").arg(*path),
                          {{u"path"_s, *path}});
            return std::nullopt;
        }
        *id = *path;
        return current.text.left(MaxTemplateChars);
    }
    const QString name = r.templ.builtin.isEmpty() ? defaultTemplateFor(r.place.kind == PlaceKind::Page)
                                                   : r.templ.builtin;
    const BuiltinTemplate *t = builtinTemplate(name);
    if (!t) {
        QJsonArray known;
        for (const BuiltinTemplate &b : builtinTemplates())
            known.append(b.id);
        call.setError(ExitUsage, u"unknown_template"_s, tr("No built-in template %1").arg(name),
                      {{u"template"_s, name}, {u"templates"_s, known}});
        return std::nullopt;
    }
    *id = t->id;
    return t->text;
}

/// The indexes of the picture tokens in `text`.
QSet<int> usedPictures(const QString &text)
{
    QSet<int> used;
    replacePictureTokens(text, [&used](int index, bool inLink) {
        if (!inLink)
            used.insert(index);
        return QString();
    });
    return used;
}

std::optional<Built> build(Call &call, const ClipRequest &r)
{
    Built b;
    const QDateTime now = call.ctx.now();
    const dates::UiLocale locale = call.ctx.locale();
    const QUrl base(r.baseUrl.isEmpty() ? r.url : r.baseUrl);
    const auto tpl = templateText(call, r, &b.templateId);
    if (!tpl)
        return std::nullopt;
    static const QRegularExpression wantsText(u"\\{\\{\\s*(?:content|selection)\\s*:\\s*text\\s*\\}\\}"_s,
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wantsImage(u"\\{\\{\\s*image\\s*\\}\\}"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wantsScreenshot(u"\\{\\{\\s*screenshot\\s*\\}\\}"_s,
                                                    QRegularExpression::CaseInsensitiveOption);
    const bool plain = wantsText.match(*tpl).hasMatch();

    ClipValues v;
    v.url = r.kind == ClipKind::Link ? r.linkUrl : r.url;
    v.title = withoutTokenChars(r.title.isEmpty() ? fallbackTitle(r, now, locale) : r.title);
    if (r.kind == ClipKind::Link && !r.linkText.isEmpty())
        v.title = withoutTokenChars(r.linkText);
    v.domain = hostOf(v.url);
    v.site = withoutTokenChars(r.kind == ClipKind::Link ? QString() : r.site);
    v.author = withoutTokenChars(r.author);
    v.published = withoutTokenChars(r.published);
    v.description = withoutTokenChars(r.description);
    v.tags = r.tags;

    if (!r.selectionHtml.trimmed().isEmpty()) {
        const ConvertedHtml selection = convertHtml(r.selectionHtml, base, b.pictures, plain);
        v.selection = selection.markdown.trimmed();
        v.selectionText = selection.plainText;
    }
    if (v.selection.isEmpty() && !r.selectionText.trimmed().isEmpty()) {
        v.selection = textAsMarkdown(withoutTokenChars(r.selectionText));
        v.selectionText = withoutTokenChars(r.selectionText).trimmed();
    }
    switch (r.kind) {
    case ClipKind::Article:
    case ClipKind::Page:
        if (!r.html.trimmed().isEmpty()) {
            const ConvertedHtml content = convertHtml(r.html, base, b.pictures, plain);
            v.content = content.markdown.trimmed();
            v.contentText = content.plainText;
        }
        if (v.content.isEmpty()) {
            v.content = v.selection;
            v.contentText = v.selectionText;
        }
        break;
    case ClipKind::Selection:
    case ClipKind::Bookmark:
        v.content = v.selection;
        v.contentText = v.selectionText;
        break;
    case ClipKind::Screenshot: v.content = b.pictures.add(r.screenshot, v.title); break;
    case ClipKind::Picture: v.content = b.pictures.add(r.pictureUrl, v.title); break;
    case ClipKind::Link: break;
    }
    if (!r.screenshot.isEmpty() && wantsScreenshot.match(*tpl).hasMatch())
        v.screenshot = b.pictures.add(r.screenshot, v.title);
    if (!r.image.isEmpty() && wantsImage.match(*tpl).hasMatch())
        v.image = b.pictures.add(r.image, v.title);

    b.title = v.title;
    b.text = fillClipTemplate(*tpl, v, TemplateFill{now, locale});
    if (r.place.kind != PlaceKind::Page)
        b.text = frontmatterAsBlockProperties(b.text);
    return b;
}

/// A Markdown image of an address (a picture left on the internet).
QString remotePicture(const ClipPicture &picture)
{
    if (!webpicture::isWebUrl(picture.source))
        return picture.alt.isEmpty() ? QString() : io::escapeMarkdownText(picture.alt, false);
    QString destination = picture.source;
    destination.replace(u' ', u"%20"_s).replace(u'(', u"%28"_s).replace(u')', u"%29"_s);
    return u"!["_s + io::escapeMarkdownText(picture.alt, false, true) + u"]("_s + destination + u')';
}

/// The page's file name for a title: file-name-safe, at most MaxFileNameChars characters.
QString pageName(const QString &title)
{
    return paths::sanitizeFileName(cutText(title, MaxFileNameChars));
}

/// A link to `path` for the journal: `[[Title]]` when no other page has that title, else the path.
QString pageLink(Vault &v, const VaultPath &path)
{
    const QString title = paths::pageTitle(path);
    int same = 0;
    for (const FileStat &f : v.markdownFiles()) {
        if (paths::pageTitle(f.path).compare(title, Qt::CaseInsensitive) == 0 && f.path != path)
            ++same;
    }
    return u"[["_s + (same == 0 ? title : paths::stripMarkdownExtension(path)) + u"]]"_s;
}

/// The page a clip of `title` becomes in `folder`: never an existing page (a name differing only in
/// letter case is one), never a daily note (a title that is a date).
notebook::CreatedPage plannedClipPage(Vault &v, const QString &title, const ClipPlace &place, const QString &suffix)
{
    const QString folder = place.defaultFolder ? QString() : place.folder;
    QString name = pageName(title);
    for (int n = 1; n <= 50; ++n) {
        const QString candidate = n == 1 ? name : name + u' ' + QString::number(n);
        notebook::CreatedPage planned = notebook::plannedPage(v, candidate, folder);
        if (planned.path.isEmpty())
            return planned;
        const auto journal = markdown::journalDateFromPath(planned.path, v.config());
        if (journal && n == 1) {
            name += u" – "_s + suffix;
            --n;
            continue;
        }
        if (!planned.existing)
            return planned;
    }
    return notebook::plannedPage(v, name, folder);
}

QString reasonText(const QString &reason)
{
    if (reason == u"too_many")
        return tr("more pictures than one clip saves");
    if (reason == u"total_size")
        return tr("the clip's pictures are too large together");
    if (reason == u"private_host")
        return tr("on this computer or its network, while the page is on the internet");
    if (reason == u"too_large")
        return tr("too large");
    if (reason == u"not_picture")
        return tr("not a picture");
    if (reason == u"timeout")
        return tr("no answer in time");
    if (reason == u"http")
        return tr("the server refused it");
    if (reason == u"redirect")
        return tr("a redirect that is not allowed");
    if (reason == u"bad_data")
        return tr("malformed data");
    if (reason == u"cancelled")
        return tr("cancelled");
    return tr("network error");
}

} // namespace

Result requestFailure(Call &call, const RequestProblem &problem)
{
    return call.fail(ExitUsage, problem.code, problem.message, {{u"field"_s, problem.field}});
}

QJsonArray clipTemplates(Call &call)
{
    QJsonArray out;
    for (const BuiltinTemplate &t : builtinTemplates())
        out.append(QJsonObject{{u"id"_s, t.id}, {u"name"_s, QString::fromUtf8(t.name)}, {u"builtin"_s, true}});
    Vault *v = call.vault();
    if (!v)
        return out;
    const QString folder = paths::normalize(v->config().templatesFolder);
    if (folder.isEmpty())
        return out;
    const VaultConfig config = v->config();
    for (const FileStat &f : v->markdownFiles()) {
        if (!f.path.startsWith(folder + u'/'))
            continue;
        // The journal's and periodic notes' templates fill days, not clips.
        if (f.path == config.journalTemplate || f.path == config.weeklyTemplate || f.path == config.monthlyTemplate)
            continue;
        out.append(QJsonObject{{u"page"_s, f.path}, {u"name"_s, paths::pageTitle(f.path)}, {u"builtin"_s, false}});
        if (out.size() >= 200)
            break;
    }
    return out;
}

Result runClip(Call &call, const ClipRequest &r, bool preview, const ClipProgress &progress)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    auto built = build(call, r);
    if (!built)
        return call.failed();
    const QDateTime now = call.ctx.now();
    const QString attachments = paths::normalize(v->config().attachmentsFolder);
    const QList<ClipPicture> &pictures = built->pictures.pictures();
    const QSet<int> used = usedPictures(built->text);

    // Where it goes (checked before anything is downloaded or written).
    VaultPath target;
    notebook::CreatedPage planned;
    if (r.place.kind == PlaceKind::Page) {
        planned = plannedClipPage(*v, built->title, r.place, hostOf(r.url));
        if (planned.path.isEmpty())
            return failWith(problemError(planned.problem));
        target = planned.path;
    } else if (r.place.kind == PlaceKind::Journal) {
        target = notebook::journalFileFor(*v, now.date());
    } else {
        const auto path = call.vaultPath(r.place.page);
        if (!path)
            return call.failed();
        if (!call.nb()->read(*path).exists)
            return call.fail(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(*path), {{u"path"_s, *path}});
        target = *path;
    }

    // The pictures: downloaded and saved (not in a preview or a dry run), else left on the internet.
    QHash<int, VaultPath> saved;
    QJsonArray failed;
    int kept = 0;
    const bool download = r.downloadPictures && !preview && !call.dryRun() && !used.isEmpty();
    if (download) {
        if (!call.lock())
            return call.failed();
        QList<ClipPicture> wanted;
        QList<int> indexes;
        for (int i = 0; i < pictures.size(); ++i) {
            if (used.contains(i)) {
                wanted.append(pictures.at(i));
                indexes.append(i);
            }
        }
        PictureLimits limits;
        // A page on this computer or its network may show pictures from there.
        const QUrl page(r.url);
        limits.privateHosts = page.isLocalFile() || webpicture::isPrivateHost(page.host());
        PictureBatch batch;
        QEventLoop loop;
        QObject::connect(&batch, &PictureBatch::finished, &loop, &QEventLoop::quit);
        if (progress)
            QObject::connect(&batch, &PictureBatch::progress, &loop, progress);
        batch.start(wanted, limits);
        if (!batch.isFinished())
            loop.exec();
        const QString stem = pageName(built->title);
        int dataNumber = 0;
        for (qsizetype k = 0; k < wanted.size(); ++k) {
            const PictureResult &result = batch.results().at(k);
            const ClipPicture &picture = wanted.at(k);
            if (result.error.isEmpty()) {
                const bool data = picture.source.startsWith(u"data:"_s, Qt::CaseInsensitive);
                const QString name = pictureFileName(picture, result.format, stem, data ? ++dataNumber : 0);
                const VaultPath path = v->createBinary(paths::join(attachments, name), result.data, ChangeSource::User);
                if (!path.isEmpty()) {
                    saved.insert(indexes.at(k), path);
                    continue;
                }
                failed.append(QJsonObject{{u"source"_s, cutText(picture.source, 300)},
                                          {u"reason"_s, u"io_error"_s},
                                          {u"message"_s, v->errorString()}});
                continue;
            }
            failed.append(QJsonObject{{u"source"_s, cutText(picture.source, 300)},
                                      {u"reason"_s, result.error},
                                      {u"message"_s, reasonText(result.error)}});
        }
    }
    // Saved pictures by name when no other file of the notebook has it (as the editor embeds a
    // pasted picture), else by path.
    QHash<QString, int> names;
    if (!saved.isEmpty()) {
        for (const FileStat &f : v->files())
            ++names[paths::basename(f.path).toCaseFolded()];
    }
    const auto embed = [&](int index, bool inLink) -> QString {
        if (index < 0 || index >= pictures.size())
            return {};
        const ClipPicture &picture = pictures.at(index);
        const auto it = saved.constFind(index);
        if (it == saved.cend()) {
            if (!inLink)
                ++kept;
            return inLink ? QString() : remotePicture(picture);
        }
        if (inLink) {
            // Inside a link only Markdown's picture syntax works: the vault path, encoded.
            return u"!["_s + io::escapeMarkdownText(picture.alt, false, true) + u"]("_s
                   + QString::fromLatin1(QUrl::toPercentEncoding(*it, "/")) + u')';
        }
        const QString name = paths::basename(*it);
        return u"![["_s + (names.value(name.toCaseFolded()) <= 1 ? name : *it) + u"]]"_s;
    };
    const QString text = replacePictureTokens(built->text, embed);
    // Pictures inside links are counted once, as pictures of the text.
    QJsonObject pictureJson{{u"total"_s, int(used.size())},
                            {u"saved"_s, int(saved.size())},
                            {u"kept"_s, kept},
                            {u"failed"_s, failed}};

    QJsonObject out{{u"title"_s, built->title},
                    {u"kind"_s, clipKindName(r.kind)},
                    {u"place"_s, placeKindName(r.place.kind)},
                    {u"template"_s, built->templateId},
                    {u"pictures"_s, pictureJson}};
    if (preview) {
        out.insert(u"path"_s, target);
        out.insert(u"markdown"_s, cutText(text, MaxPreviewChars));
        out.insert(u"truncated"_s, text.size() > MaxPreviewChars);
        return call.ok(out, text);
    }

    if (r.place.kind == PlaceKind::Page) {
        if (call.dryRun()) {
            out.insert(u"path"_s, planned.path);
            out.insert(u"created"_s, false);
            out.insert(u"dryRun"_s, true);
            out.insert(u"diff"_s, unifiedDiff(QString(), notebook::newPageText(*v, planned.path, std::nullopt, text),
                                              planned.path));
            return call.ok(out);
        }
        if (!call.lock())
            return call.failed();
        const QString folder = r.place.defaultFolder ? QString() : r.place.folder;
        const notebook::CreatedPage page =
                notebook::createPage(*v, paths::pageTitle(planned.path), folder, std::nullopt, text, ChangeSource::User);
        if (page.path.isEmpty() || page.existing)
            return failWith(problemError(page.problem.isOk() ? notebook::PathProblem{notebook::PathProblem::PageExists,
                                                                                     paths::pageTitle(page.path), {}}
                                                             : page.problem));
        out.insert(u"path"_s, page.path);
        out.insert(u"created"_s, true);
        out.insert(u"line"_s, 0);
        out.insert(u"ref"_s, Call::lineRef(page.path, call.nb()->read(page.path).text, 0));
        out.insert(u"hash"_s, call.nb()->read(page.path).hash);
        out.insert(u"open"_s, QJsonObject{{u"path"_s, page.path}, {u"line"_s, 0}});
        if (r.place.journalLink) {
            const VaultPath journal = notebook::journalFileFor(*v, now.date());
            const QString block = notebook::blockFromText(pageLink(*v, page.path));
            int line = -1;
            const auto written = call.edit(
                    journal,
                    [&](const QString &current, bool, QString *next) {
                        *next = notebook::appendBlock(current, block);
                        line = appendedLine(*next, block);
                        return true;
                    },
                    creatingJournal(call, journal));
            if (!written)
                return call.failed();
            out.insert(u"journal"_s, QJsonObject{{u"path"_s, journal},
                                                 {u"line"_s, line},
                                                 {u"ref"_s, Call::lineRef(journal, written->after, line)}});
        }
        return call.ok(out, page.path);
    }

    const QString block = notebook::blockFromText(text);
    if (block.isEmpty())
        return call.fail(ExitUsage, u"empty_clip"_s, tr("The template left nothing to write"));
    int line = -1;
    const notebook::Notebook::WriteOptions options =
            r.place.kind == PlaceKind::Journal ? creatingJournal(call, target) : notebook::Notebook::WriteOptions();
    const auto written = call.edit(
            target,
            [&](const QString &current, bool, QString *next) {
                if (r.place.kind == PlaceKind::Journal) {
                    *next = notebook::appendBlock(current, block);
                    line = appendedLine(*next, block);
                    return true;
                }
                const auto placed = insertUnder(call, current, r.place.heading, block, &line);
                if (!placed)
                    return false;
                *next = *placed;
                return true;
            },
            options);
    if (!written)
        return call.failed();
    QJsonObject w = call.writeJson(*written);
    for (auto it = w.constBegin(); it != w.constEnd(); ++it)
        out.insert(it.key(), it.value());
    out.insert(u"line"_s, line);
    out.insert(u"ref"_s, Call::lineRef(target, written->after, line));
    out.insert(u"open"_s, QJsonObject{{u"path"_s, target}, {u"line"_s, line}});
    return call.ok(out, target);
}

namespace {

/// `clip save`: the arguments as a request.
Result clipSave(Call &call)
{
    QJsonObject request;
    for (const QString &key : {u"url"_s, u"title"_s, u"author"_s, u"site"_s, u"published"_s, u"description"_s,
                               u"linkUrl"_s, u"linkText"_s, u"pictureUrl"_s, u"baseUrl"_s}) {
        if (call.has(key))
            request.insert(key, call.str(key));
    }
    const QString html = call.text(u"html"_s);
    if (!html.isEmpty())
        request.insert(u"html"_s, html);
    if (call.has(u"selection"_s))
        request.insert(u"selectionHtml"_s, call.str(u"selection"_s));
    QString kind = call.has(u"kind"_s) ? call.str(u"kind"_s) : QString();
    if (kind.isEmpty())
        kind = !html.isEmpty()              ? u"article"_s
               : call.has(u"selection"_s)   ? u"selection"_s
               : call.has(u"pictureUrl"_s)  ? u"picture"_s
               : call.has(u"linkUrl"_s)     ? u"link"_s
                                            : u"bookmark"_s;
    request.insert(u"kind"_s, kind);
    if (call.has(u"tags"_s))
        request.insert(u"tags"_s, call.str(u"tags"_s));
    request.insert(u"downloadPictures"_s, !call.flag(u"noPictures"_s));

    QJsonObject place{{u"kind"_s, call.has(u"place"_s) ? call.str(u"place"_s) : u"page"_s}};
    if (call.has(u"folder"_s))
        place.insert(u"folder"_s, call.str(u"folder"_s) == u"." ? QString() : call.str(u"folder"_s));
    if (call.has(u"page"_s)) {
        // A page as the command line names pages: a path, a title, an alias.
        const auto ref = call.page(call.str(u"page"_s), true);
        if (!ref)
            return call.failed();
        place.insert(u"page"_s, ref->path);
        if (!call.has(u"place"_s))
            place.insert(u"kind"_s, u"heading"_s);
    }
    if (call.has(u"heading"_s))
        place.insert(u"heading"_s, call.str(u"heading"_s));
    if (call.flag(u"journalLink"_s))
        place.insert(u"journalLink"_s, true);
    request.insert(u"place"_s, place);
    if (call.has(u"template"_s)) {
        const QString name = call.str(u"template"_s).trimmed();
        if (builtinTemplate(name)) {
            request.insert(u"template"_s, QJsonObject{{u"builtin"_s, name}});
        } else {
            const auto ref = call.page(name, true);
            if (!ref)
                return call.failed();
            request.insert(u"template"_s, QJsonObject{{u"page"_s, ref->path}});
        }
    }
    RequestProblem problem;
    const auto parsed = parseClipRequest(request, &problem);
    if (!parsed)
        return requestFailure(call, problem);
    return runClip(call, *parsed, call.flag(u"preview"_s));
}

Result clipHostCommand(Call &call)
{
    // run.cpp serves the host itself (it needs standard input and output for the frames).
    return call.fail(ExitUsage, u"host_mode"_s, tr("clip host speaks native messaging: a browser starts it"));
}

} // namespace

// clipinstall.cpp
Result clipInstall(Call &call);
Result clipUninstall(Call &call);
Result clipStatus(Call &call);
Result clipManifest(Call &call);
QList<Arg> clipInstallArgs(bool install);

void addClipCommands(QList<Command> &out)
{
    Command c;
    c.id = u"clip.save"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "Save a web page's HTML, a link or a picture as a clip (a page, a block of "
                                          "today's journal or of a heading's section)"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "What the Web Clipper does (docs/CLIPPER.md): the HTML (--html or --stdin) becomes Markdown, its "
            "relative links and pictures resolved against --url (or --base-url), its pictures saved into the "
            "attachments folder (http(s) only, within limits; --no-pictures leaves them on the internet), all "
            "filled into a template (--template: brulion, obsidian, joplin, link, or a template page)."));
    c.run = clipSave;
    c.writes = true;
    c.args = {
            option(u"url"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The page's address")),
            option(u"title"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The title (the new page's name)")),
            stdinable(option(u"html"_s, ArgType::Text,
                             QT_TRANSLATE_NOOP("brulion::cli", "The HTML of the article or the page"))),
            option(u"selection"_s, ArgType::Text,
                   QT_TRANSLATE_NOOP("brulion::cli", "The HTML of the selection ({{selection}})")),
            choice(u"kind"_s, {u"article"_s, u"selection"_s, u"page"_s, u"bookmark"_s, u"picture"_s, u"link"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "What was clipped (default: from the arguments)")),
            choice(u"place"_s, {u"page"_s, u"journal"_s, u"heading"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Where it goes (default: a new page)")),
            option(u"folder"_s, ArgType::Path,
                   QT_TRANSLATE_NOOP("brulion::cli", "The new page's folder (default: the folder for new pages; . = root)")),
            option(u"page"_s, ArgType::Ref,
                   QT_TRANSLATE_NOOP("brulion::cli", "The page with the heading (--place heading)")),
            option(u"heading"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The heading whose section gets the clip (\"## Do przeczytania\")")),
            option(u"template"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "brulion, obsidian, joplin, link, or a template page")),
            option(u"tags"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Tags, separated by commas")),
            option(u"author"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The author ({{author}})")),
            option(u"site"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The site's name ({{site}})")),
            option(u"published"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The publication date ({{published}})")),
            option(u"description"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The description ({{description}})")),
            option(u"baseUrl"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The address relative links are resolved against (default: --url)")),
            option(u"linkUrl"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A link's address (--kind link)")),
            option(u"linkText"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A link's text (--kind link)")),
            option(u"pictureUrl"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "A picture's address (--kind picture)")),
            flag(u"noPictures"_s, QT_TRANSLATE_NOOP("brulion::cli", "Leave the pictures on the internet")),
            flag(u"journalLink"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "Also a link to the new page in today's journal")),
            flag(u"preview"_s, QT_TRANSLATE_NOOP("brulion::cli", "Only show the Markdown (nothing downloaded or written)")),
    };
    out.append(c);

    c = Command();
    c.id = u"clip.host"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "The Web Clipper's native messaging host (the browser starts it)"));
    c.run = clipHostCommand;
    c.needsVault = false;
    c.mcp = false;
    out.append(c);

    c = Command();
    c.id = u"clip.install"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "Connect the browsers' Web Clipper to brulion-cli (the native messaging host manifests)"));
    c.run = clipInstall;
    c.args = clipInstallArgs(true);
    c.needsVault = false;
    c.mcp = false;
    c.writes = true;
    out.append(c);

    c = Command();
    c.id = u"clip.uninstall"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Remove the Web Clipper's host manifests"));
    c.run = clipUninstall;
    c.args = clipInstallArgs(false);
    c.needsVault = false;
    c.mcp = false;
    c.writes = true;
    out.append(c);

    c = Command();
    c.id = u"clip.status"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "Which browsers can reach brulion-cli (the host manifests and their program)"));
    c.run = clipStatus;
    c.args = clipInstallArgs(false);
    c.needsVault = false;
    c.mcp = false;
    out.append(c);

    c = Command();
    c.id = u"clip.manifest"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Print a host manifest (for packages)"));
    c.run = clipManifest;
    c.args = {choice(u"browser"_s, {u"chrome"_s, u"chromium"_s, u"brave"_s, u"edge"_s, u"firefox"_s},
                     QT_TRANSLATE_NOOP("brulion::cli", "The browser (chrome, chromium, brave and edge share one)"),
                     true, true),
              option(u"program"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "The program the browser starts (default: this one)")),
              option(u"extensionId"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "More Chromium extension ids, separated by commas"))};
    c.needsVault = false;
    c.mcp = false;
    out.append(c);
}

} // namespace brulion::cli
