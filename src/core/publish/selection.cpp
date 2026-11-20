#include "publish/selection.h"

#include "chat/chat.h"
#include "index/vaultindex.h"
#include "markdown/encrypted.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

/// A property's value as words: "true", "nie", "1" (lower case, trimmed).
QString flagWord(const QVariant &value)
{
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? u"true"_s : u"false"_s;
    if (value.typeId() == QMetaType::QStringList || value.typeId() == QMetaType::QVariantList) {
        const QStringList list = value.toStringList();
        return list.size() == 1 ? list.first().trimmed().toLower() : QString();
    }
    return value.toString().trimmed().toLower();
}

std::optional<bool> flagOf(const QString &word)
{
    static const QStringList yes{u"true"_s, u"yes"_s, u"tak"_s, u"1"_s, u"on"_s, u"prawda"_s};
    static const QStringList no{u"false"_s, u"no"_s, u"nie"_s, u"0"_s, u"off"_s, u"fałsz"_s};
    if (yes.contains(word))
        return true;
    if (no.contains(word))
        return false;
    return std::nullopt;
}

/// A property of the page (`key::` lines or frontmatter), any letter case of `key`.
QVariant pageProperty(const ParsedNote &note, const QStringList &keys)
{
    for (const QString &key : keys) {
        for (auto it = note.properties.cbegin(); it != note.properties.cend(); ++it) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
                return it.value();
        }
        for (auto it = note.frontmatter.cbegin(); it != note.frontmatter.cend(); ++it) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
                return it.value();
        }
    }
    return {};
}

/// The page-level tags of a note (frontmatter `tags:` and a `tags::` line), lower case, no '#'.
QStringList pageTags(const ParsedNote &note)
{
    QStringList out;
    const auto add = [&out](const QVariant &value) {
        QStringList words;
        if (value.typeId() == QMetaType::QStringList || value.typeId() == QMetaType::QVariantList) {
            words = value.toStringList();
        } else {
            static const QRegularExpression separators(u"[,\\s]+"_s);
            words = value.toString().split(separators, Qt::SkipEmptyParts);
        }
        for (QString w : std::as_const(words)) {
            w = w.trimmed();
            while (w.startsWith(u'#'))
                w.remove(0, 1);
            w = w.toLower();
            if (!w.isEmpty() && !out.contains(w))
                out.append(w);
        }
    };
    add(note.properties.value(u"tags"_s));
    add(note.frontmatter.value(u"tags"_s));
    return out;
}

bool inFolder(const VaultPath &path, const QString &folder)
{
    return folder.isEmpty() || path.startsWith(folder + u'/');
}

bool isTagOrSubTag(const QString &tag, const QString &wanted)
{
    return tag == wanted || tag.startsWith(wanted + u'/');
}

QString cleanTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(u'#'))
        tag.remove(0, 1);
    return tag.toLower();
}

/// The end of a fenced code block opened at `lines[start]` (index of its closing line, or the
/// last line when it is not closed).
qsizetype fenceEnd(const QStringList &lines, qsizetype start, const QString &fence)
{
    for (qsizetype i = start + 1; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t.startsWith(fence) && t.mid(fence.size()).trimmed().isEmpty()
            && !t.mid(fence.size()).contains(fence.at(0)))
            return i;
    }
    return lines.size() - 1;
}

/// Does the frontmatter say the page is encrypted (`encrypted: true`, `zaszyfrowane: tak`)?
bool frontmatterSaysEncrypted(const QString &content)
{
    if (!content.startsWith(u"---"_s))
        return false;
    const qsizetype end = content.indexOf(u"\n---"_s, 3);
    const QString head = content.mid(0, end < 0 ? content.size() : end);
    static const QRegularExpression key(u"^\\s*(encrypted|zaszyfrowan[aey])\\s*:\\s*(\\S+)"_s,
                                        QRegularExpression::MultilineOption
                                                | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = key.match(head);
    return m.hasMatch() && flagOf(m.captured(2).toLower()).value_or(false);
}

} // namespace

QString itemKindName(PublishItem::Kind kind)
{
    switch (kind) {
    case PublishItem::Kind::Page: return u"page"_s;
    case PublishItem::Kind::Folder: return u"folder"_s;
    case PublishItem::Kind::Tag: return u"tag"_s;
    case PublishItem::Kind::Notebook: return u"notebook"_s;
    }
    return u"page"_s;
}

std::optional<PublishItem::Kind> itemKindFromName(QStringView name)
{
    if (name == u"page")
        return PublishItem::Kind::Page;
    if (name == u"folder")
        return PublishItem::Kind::Folder;
    if (name == u"tag")
        return PublishItem::Kind::Tag;
    if (name == u"notebook")
        return PublishItem::Kind::Notebook;
    return std::nullopt;
}

bool PublishScope::add(const PublishItem &item)
{
    PublishItem clean = item;
    if (clean.kind == PublishItem::Kind::Tag)
        clean.value = cleanTag(clean.value);
    else if (clean.kind == PublishItem::Kind::Notebook)
        clean.value.clear();
    else
        clean.value = paths::normalize(clean.value);
    if ((clean.kind != PublishItem::Kind::Notebook && clean.value.isEmpty()) || items.contains(clean))
        return false;
    items.append(clean);
    return true;
}

QString reasonName(LeftOutReason reason)
{
    switch (reason) {
    case LeftOutReason::Hidden: return u"hidden"_s;
    case LeftOutReason::Private: return u"private"_s;
    case LeftOutReason::NotMarked: return u"not-marked"_s;
    case LeftOutReason::Extension: return u"extension"_s;
    case LeftOutReason::Conversation: return u"conversation"_s;
    case LeftOutReason::Template: return u"template"_s;
    case LeftOutReason::Encrypted: return u"encrypted"_s;
    case LeftOutReason::ConflictCopy: return u"conflict-copy"_s;
    }
    return {};
}

std::optional<bool> publishFlag(const ParsedNote &note)
{
    const QVariant value = pageProperty(note, {u"publish"_s, u"publikuj"_s});
    if (!value.isValid() || value.isNull())
        return std::nullopt;
    return flagOf(flagWord(value));
}

bool isExtensionPage(const ParsedNote &note)
{
    const QStringList tags = pageTags(note);
    return tags.contains(u"extension"_s) || tags.contains(u"rozszerzenie"_s);
}

bool isHiddenPath(const VaultPath &path)
{
    if (paths::isIgnored(path))
        return true;
    const QStringList segments = path.split(u'/', Qt::SkipEmptyParts);
    return std::ranges::any_of(segments, [](const QString &s) { return s.startsWith(u'.'); });
}

bool isEncryptedFile(const VaultPath &path)
{
    if (markdown::isEncryptedNotePath(path))
        return true;
    static const QStringList extensions{u".mdenc"_s, u".encrypted"_s, u".enc"_s, u".age"_s,
                                        u".gpg"_s,   u".pgp"_s,       u".asc"_s, u".aes"_s};
    return std::ranges::any_of(extensions, [&](const QString &e) { return path.endsWith(e, Qt::CaseInsensitive); });
}

StrippedText stripEncrypted(const QString &content)
{
    StrippedText out;
    out.wholePage = frontmatterSaysEncrypted(content);
    // 1. Fenced blocks of encrypted text, whole lines (their fences included).
    static const QStringList languages{u"encrypted"_s, u"encrypt"_s,          u"crypt"_s, u"secret"_s, u"age"_s,
                                       u"pgp"_s,       u"brulion-encrypted"_s};
    static const QRegularExpression opening(u"^\\s*(?:[-*+]\\s+|\\d+[.)]\\s+)?(`{3,}|~{3,})\\s*([A-Za-z0-9_-]*)"_s);
    QString text = content;
    if (text.contains(u"```"_s) || text.contains(u"~~~"_s)) {
        QStringList lines = text.split(u'\n');
        QStringList kept;
        kept.reserve(lines.size());
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QRegularExpressionMatch m = opening.match(lines.at(i));
            if (!m.hasMatch()) {
                kept.append(lines.at(i));
                continue;
            }
            const qsizetype end = fenceEnd(lines, i, m.captured(1));
            if (languages.contains(m.captured(2).toLower())) {
                ++out.removed;
            } else {
                for (qsizetype j = i; j <= end; ++j)
                    kept.append(lines.at(j));
            }
            i = end;
        }
        if (out.removed > 0)
            text = kept.join(u'\n');
    }
    // 2. Meld Encrypt's in-place fragments as the encrypted notes read them (markdown/encrypted.h:
    //    every version, hidden or visible), then — for anything that only looks like one, and for
    //    armoured PGP / age messages — the patterns below.
    if (markdown::mayContainEncrypted(text)) {
        const qsizetype fragments = markdown::findEncryptedFragments(text).size();
        if (fragments > 0) {
            text = markdown::replaceEncryptedFragments(text, QStringView());
            out.removed += int(fragments);
        }
    }
    static const QRegularExpression meld(u"(?:%%)?\U0001F510[^\\s]*\\s.*?\U0001F510(?:%%)?"_s,
                                         QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression armoured(
            u"-----BEGIN (?:PGP MESSAGE|AGE ENCRYPTED FILE)-----.*?-----END (?:PGP MESSAGE|AGE ENCRYPTED FILE)-----"_s,
            QRegularExpression::DotMatchesEverythingOption);
    for (const QRegularExpression *re : {&meld, &armoured}) {
        if (!text.contains(re == &meld ? u"\U0001F510"_s : u"-----BEGIN "_s))
            continue;
        QString result;
        qsizetype from = 0;
        for (QRegularExpressionMatchIterator it = re->globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            result += QStringView(text).sliced(from, m.capturedStart() - from);
            from = m.capturedEnd();
            ++out.removed;
        }
        if (from > 0) {
            result += QStringView(text).sliced(from);
            text = result;
        }
    }
    out.text = text;
    if (out.removed > 0 && !out.wholePage) {
        // Nothing but frontmatter and blank lines (or empty list markers) left: the whole page.
        QString rest = text;
        if (rest.startsWith(u"---"_s)) {
            const qsizetype end = rest.indexOf(u"\n---"_s, 3);
            rest = end < 0 ? QString() : rest.mid(end + 4);
        }
        static const QRegularExpression blank(u"^[\\s\\-*+>]*$"_s);
        out.wholePage = blank.match(rest).hasMatch();
    }
    return out;
}

PublishSelection selectFiles(const VaultIndex &index, const PublishScope &scope)
{
    PublishSelection out;
    const VaultConfig &config = index.config();
    const QString templates = paths::normalize(config.templatesFolder);
    const QString conversations = chat::folder();
    QHash<VaultPath, LeftOutReason> leftOut;
    QSet<VaultPath> explicitPages;
    QSet<VaultPath> candidates;
    const QList<VaultPath> all = index.paths();

    // The pages the items name.
    for (const PublishItem &item : scope.items) {
        qsizetype found = 0;
        switch (item.kind) {
        case PublishItem::Kind::Page: {
            const VaultPath path = paths::normalize(item.value);
            if (index.note(path)) {
                explicitPages.insert(path);
                candidates.insert(path);
                found = 1;
            }
            break;
        }
        case PublishItem::Kind::Folder: {
            const QString folder = paths::normalize(item.value);
            for (const VaultPath &p : all) {
                if (!folder.isEmpty() && inFolder(p, folder)) {
                    candidates.insert(p);
                    ++found;
                }
            }
            break;
        }
        case PublishItem::Kind::Tag: {
            const QString wanted = cleanTag(item.value);
            for (const QList<TagCount> tags = index.tags(); const TagCount &t : tags) {
                if (wanted.isEmpty() || !isTagOrSubTag(t.tag, wanted))
                    continue;
                for (const VaultPath &p : t.paths) {
                    candidates.insert(p);
                    ++found;
                }
            }
            break;
        }
        case PublishItem::Kind::Notebook:
            for (const VaultPath &p : all)
                candidates.insert(p);
            found = all.size();
            break;
        }
        if (found == 0)
            out.missingItems.append(item);
    }

    // A page's verdict: published, or left out with the reason.
    const auto verdict = [&](const VaultPath &path, bool chosen) -> std::optional<LeftOutReason> {
        if (isHiddenPath(path))
            return LeftOutReason::Hidden;
        if (paths::isConflictCopy(path))
            return LeftOutReason::ConflictCopy;
        const ParsedNote *note = index.note(path);
        if (!note)
            return LeftOutReason::Hidden;
        if (isEncryptedFile(path) || stripEncrypted(index.content(path)).wholePage)
            return LeftOutReason::Encrypted;
        const std::optional<bool> flag = publishFlag(*note);
        if (flag == false)
            return LeftOutReason::Private;
        if (!chosen) {
            if (!templates.isEmpty() && inFolder(path, templates))
                return LeftOutReason::Template;
            if (!scope.extensionPages && isExtensionPage(*note))
                return LeftOutReason::Extension;
            if (!scope.aiConversations && inFolder(path, conversations))
                return LeftOutReason::Conversation;
            if (scope.onlyMarked && flag != true)
                return LeftOutReason::NotMarked;
        } else if (!scope.extensionPages && isExtensionPage(*note) && flag != true) {
            // An extension page is code: chosen by itself it still needs the option (or a mark).
            return LeftOutReason::Extension;
        }
        return std::nullopt;
    };

    QSet<VaultPath> pages;
    for (const VaultPath &p : std::as_const(candidates)) {
        if (const auto reason = verdict(p, explicitPages.contains(p)))
            leftOut.insert(p, *reason);
        else
            pages.insert(p);
    }
    // One step of links from the items' pages.
    if (scope.linkedPages) {
        const QSet<VaultPath> first = pages;
        for (const VaultPath &p : first) {
            for (const QList<ResolvedLink> links = index.outgoingLinks(p); const ResolvedLink &l : links) {
                if (l.target.isEmpty() || pages.contains(l.target) || leftOut.contains(l.target)
                    || !index.note(l.target))
                    continue;
                if (const auto reason = verdict(l.target, false))
                    leftOut.insert(l.target, *reason);
                else
                    pages.insert(l.target);
            }
        }
    }
    // The attachments of the published pages.
    QSet<VaultPath> attachments;
    const auto addAttachment = [&](const VaultPath &target) {
        if (target.isEmpty() || index.note(target) || attachments.contains(target) || leftOut.contains(target))
            return;
        if (!index.contains(target))
            return;
        if (isHiddenPath(target))
            leftOut.insert(target, LeftOutReason::Hidden);
        else if (paths::isConflictCopy(target))
            leftOut.insert(target, LeftOutReason::ConflictCopy);
        else if (isEncryptedFile(target))
            leftOut.insert(target, LeftOutReason::Encrypted);
        else
            attachments.insert(target);
    };
    for (const VaultPath &p : std::as_const(pages)) {
        for (const QList<ResolvedLink> links = index.outgoingLinks(p); const ResolvedLink &l : links)
            addAttachment(l.target);
        // An Excalidraw drawing's picture beside it (the plugin's export).
        if (p.endsWith(u".excalidraw.md"_s, Qt::CaseInsensitive)) {
            const QString base = p.chopped(3);
            for (const QString &ext : {u".svg"_s, u".png"_s})
                addAttachment(base + ext);
        }
    }
    out.pages = QList<VaultPath>(pages.cbegin(), pages.cend());
    std::ranges::sort(out.pages);
    out.attachments = QList<VaultPath>(attachments.cbegin(), attachments.cend());
    std::ranges::sort(out.attachments);
    for (auto it = leftOut.cbegin(); it != leftOut.cend(); ++it)
        out.leftOut.append(LeftOut{it.key(), it.value()});
    std::ranges::sort(out.leftOut, {}, &LeftOut::path);
    return out;
}

} // namespace brulion::publish
