#include "markdown/rename.h"

#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/inline_p.h"
#include "markdown/lines_p.h"
#include "markdown/parse.h"
#include "markdown/parse_p.h"
#include "markdown/properties.h"
#include "util/paths.h"

#include <algorithm>
#include <optional>

#include <QRegularExpression>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// Comparison key: NFC, lower-case, '\' -> '/', trimmed.
QString key(QStringView s)
{
    return s.toString().normalized(QString::NormalizationForm_C).replace(u'\\', u'/').trimmed().toLower();
}

/// What to match and what to write instead.
struct RenameSpec {
    QString oldName;
    QString newName;
    QString oldNoExt; ///< empty when no old path is known
    QString newNoExt;
    bool sameDir = true; ///< the page keeps its folder: path-qualified links keep their prefix
};

RenameSpec makeSpec(const QString &oldName, const QString &newName, const VaultPath &oldPath, const VaultPath &newPath)
{
    RenameSpec spec{oldName, newName, {}, {}, true};
    if (!oldPath.isEmpty()) {
        const VaultPath old = paths::normalize(oldPath);
        spec.oldNoExt = paths::stripMarkdownExtension(old);
        spec.newNoExt = !newPath.isEmpty() ? paths::stripMarkdownExtension(paths::normalize(newPath))
                                           : paths::join(paths::dirname(old), newName);
        spec.sameDir = key(paths::dirname(spec.oldNoExt)) == key(paths::dirname(spec.newNoExt));
    }
    return spec;
}

/// Index after the last path separator of a written link target (0 when there is none).
qsizetype afterLastSlash(QStringView s)
{
    return std::max(s.lastIndexOf(u'/'), s.lastIndexOf(u'\\')) + 1;
}

QString baseName(const QString &path)
{
    return paths::basename(path);
}

/// New target for a path-qualified link to the renamed page.
QString renamedPath(QStringView written, const RenameSpec &spec)
{
    const QString target = spec.newNoExt.isEmpty() ? spec.newName : spec.newNoExt;
    if (spec.sameDir)
        return written.first(afterLastSlash(written)).toString() + baseName(target);
    return target;
}

bool endsWithMd(QStringView s)
{
    return s.endsWith(u".md", Qt::CaseInsensitive);
}

/// New wiki target for `target`, or a null QString when it does not point at the page.
QString newWikiTarget(QStringView target, const RenameSpec &spec)
{
    const QString t = key(target);
    if (t.isEmpty())
        return {};
    const QStringView trimmed = target.trimmed();
    const QString ext = endsWithMd(trimmed) ? trimmed.last(3).toString() : QString();
    const QString bare = ext.isEmpty() ? t : t.chopped(3);
    if (bare == key(spec.oldName))
        return spec.newName + ext;
    if (!spec.oldNoExt.isEmpty() && bare.contains(u'/')) {
        const QString old = key(spec.oldNoExt);
        if (bare == old || old.endsWith(u'/' + bare))
            return renamedPath(ext.isEmpty() ? trimmed : trimmed.chopped(3), spec) + ext;
    }
    return {};
}

/// How a written Markdown link destination spells its path, beyond what it must escape.
struct DestStyle {
    bool angle = false;    ///< `<dest>`
    bool spaces = false;   ///< spaces as %20 (always in a plain `(dest)`)
    bool nonAscii = false; ///< letters beyond ASCII as UTF-8 %XX ("%C5%BC": GitHub, some editors)
    bool lowerHex = false; ///< "%c5%bc"
};

/// The style of `written` (a destination's path part as written).
DestStyle destStyle(QStringView written, bool angle)
{
    DestStyle style;
    style.angle = angle;
    style.spaces = !angle || written.contains(u"%20"_s);
    static const QRegularExpression highByte(u"%[89a-fA-F][0-9a-fA-F]"_s);
    const QRegularExpressionMatch m = highByte.matchView(written);
    style.nonAscii = m.hasMatch();
    style.lowerHex = style.nonAscii && m.captured() != m.captured().toUpper();
    return style;
}

/// Percent-encode what a Markdown link destination would read differently: '%' (escapes are
/// decoded) and '#' (fragment) always; spaces and parentheses in a plain `(dest)`; '<' / '>' in
/// an angle-bracket `<dest>`; spaces and letters beyond ASCII also where `style` says the
/// destination was written so.
QString encodeDest(QString path, const DestStyle &style)
{
    path.replace(u'%', u"%25"_s).replace(u'#', u"%23"_s);
    if (style.angle)
        path.replace(u'<', u"%3C"_s).replace(u'>', u"%3E"_s);
    else
        path.replace(u'(', u"%28"_s).replace(u')', u"%29"_s);
    if (style.spaces)
        path.replace(u' ', u"%20"_s);
    if (!style.nonAscii)
        return path;
    QString out;
    out.reserve(path.size());
    for (qsizetype i = 0; i < path.size(); ++i) {
        const QChar c = path.at(i);
        if (c.unicode() < 0x80) {
            out += c;
            continue;
        }
        const qsizetype length = c.isHighSurrogate() && i + 1 < path.size() ? 2 : 1;
        const QByteArray bytes = path.sliced(i, length).toUtf8();
        i += length - 1;
        for (const char byte : bytes) {
            const QString hex = QString::number(uchar(byte), 16).rightJustified(2, u'0');
            out += u'%' + (style.lowerHex ? hex : hex.toUpper());
        }
    }
    return out;
}

/// New Markdown link path for a decoded relative `target`, or a null QString.
QString newMdTarget(const QString &target, const RenameSpec &spec)
{
    QString t = key(target);
    if (t.startsWith(u"./"))
        t = t.sliced(2);
    if (t.isEmpty())
        return {};
    const QStringView trimmed = QStringView(target).trimmed();
    const QString ext = endsWithMd(trimmed) ? trimmed.last(3).toString() : QString();
    static const QRegularExpression otherExtension(u"\\.[a-z0-9]{1,5}$"_s, QRegularExpression::CaseInsensitiveOption);
    if (ext.isEmpty() && otherExtension.match(t).hasMatch())
        return {}; // another file type
    const QString bare = ext.isEmpty() ? t : t.chopped(ext.size());
    if (!bare.contains(u'/'))
        return bare == key(spec.oldName) ? spec.newName + ext : QString();
    if (!spec.oldNoExt.isEmpty()) {
        const QString norm = key(paths::normalize(bare));
        const QString old = key(spec.oldNoExt);
        if (norm == old || old.endsWith(u'/' + norm))
            return spec.newNoExt + ext;
    }
    return {};
}

/// Rewrite one wiki link / embed source (`[[…]]`, `![[…]]`).
QString rewriteWiki(QStringView src, const RenameSpec &spec)
{
    const qsizetype open = src.startsWith(u'!') ? 3 : 2;
    const QStringView inner = src.sliced(open, src.size() - open - 2);
    qsizetype end = inner.size();
    const qsizetype hash = inner.indexOf(u'#');
    const qsizetype pipe = inner.indexOf(u'|');
    if (hash >= 0)
        end = hash;
    if (pipe >= 0 && pipe < end)
        end = pipe > 0 && inner[pipe - 1] == u'\\' ? pipe - 1 : pipe;
    const QStringView targetPart = inner.first(end);
    const QString target = newWikiTarget(targetPart, spec);
    if (target.isNull())
        return {};
    qsizetype lead = 0;
    while (lead < targetPart.size() && targetPart[lead].isSpace())
        ++lead;
    qsizetype trail = 0;
    while (trail < targetPart.size() - lead && targetPart[targetPart.size() - 1 - trail].isSpace())
        ++trail;
    return src.first(open).toString() + targetPart.first(lead) + target + targetPart.last(trail) + inner.sliced(end)
           + u"]]"_s;
}

/// Rewrite one Markdown link source (`[x](dest)` / `![x](dest)`).
QString rewriteMarkdown(QStringView src, const Link &link, const RenameSpec &spec)
{
    if (hasUrlScheme(link.target))
        return {};
    const QString target = newMdTarget(link.target, spec);
    if (target.isNull())
        return {};
    const auto parts = parseMdLink(src, src.startsWith(u'!') ? 1 : 0);
    if (!parts)
        return {};
    const QStringView dest = src.sliced(parts->destFrom, parts->destTo - parts->destFrom);
    const qsizetype hash = dest.indexOf(u'#');
    const QStringView pathPart = hash < 0 ? dest : dest.first(hash);
    const QStringView fragment = hash < 0 ? QStringView() : dest.sliced(hash);
    const DestStyle style = destStyle(pathPart, parts->angle);
    const auto encode = [&](const QString &p) { return encodeDest(p, style); };
    // A plain rename (or a bare-name link) keeps the written folder prefix, e.g. "../x/".
    const bool keepPrefix = spec.sameDir || afterLastSlash(pathPart) == 0;
    const QString newPath = keepPrefix ? pathPart.first(afterLastSlash(pathPart)).toString() + encode(baseName(target))
                                       : encode(target);
    return src.first(parts->destFrom).toString() + newPath + fragment + src.sliced(parts->destTo);
}

// ---- Tags ------------------------------------------------------------------------------------

/// A planned tag rewrite.
struct TagEdit {
    NoteTag tag;
    QString replacement;
};

/// What a tag naming the renamed page is renamed to (the written name), or a null QString.
QString renamedTagName(const QString &written, const RenameSpec &spec, const QString &oldKey, const TagFilter &naming)
{
    const QString k = key(written);
    if (k.isEmpty() || (naming && !naming(k)))
        return {};
    if (!oldKey.isEmpty() && k == oldKey)
        return spec.newName == spec.oldName ? QString() : spec.newName; // a move keeps #Dom
    if (!spec.oldNoExt.isEmpty() && k.contains(u'/')) {
        const QString old = key(spec.oldNoExt);
        if (k == old || old.endsWith(u'/' + k))
            return renamedPath(written, spec);
    }
    return {};
}

/// The text replacing tag `t` when it is renamed to `name`; null when it cannot be written.
QString tagReplacement(const NoteTag &t, const QString &name, QStringView content)
{
    switch (t.form) {
    case TagForm::Hash: return tagSyntax(name);
    case TagForm::Bracketed: {
        const QString tag = tagSyntax(name);
        if (tag.isEmpty())
            return {};
        return tag.startsWith(u"#[["_s) ? tag : u"#[["_s + tag.sliced(1) + u"]]"_s;
    }
    case TagForm::Property: {
        const QString n = name.normalized(QString::NormalizationForm_C);
        if (tagSyntax(n).isEmpty())
            return {}; // no tag can hold it
        const QChar before = charAt(content, t.from - 1);
        if (before == u'"' || before == u'\'')
            return n.contains(before) ? QString() : n;
        const bool plain = !n.contains(u',') && !n.startsWith(u'"') && !n.startsWith(u'\'') && !n.startsWith(u'#')
                           && !n.startsWith(u'[') && !n.contains(u"]]"_s);
        if (plain)
            return n;
        return u"[["_s + n + u"]]"_s;
    }
    case TagForm::Frontmatter:
        return tagSyntax(name).isEmpty() ? QString() : name.normalized(QString::NormalizationForm_C);
    }
    return {};
}

/// The rename of tags, planned: the edits of placed occurrences and the frontmatter's tag names
/// as they must read afterwards.
struct TagRenamePlan {
    QList<TagEdit> edits;
    FrontmatterTags frontmatter;
    QStringList frontmatterBefore; ///< namesFrom() of the key before
    QStringList frontmatterAfter;  ///< ... and as it must read after the rename
    int unplacedFrontmatter = 0;   ///< matching names the scan could not place
};

/// The new name of a tag as written (`written`: NFC, without '#'), or a null QString.
using TagRenamer = std::function<QString(const QString &written)>;

/// Plan the rename of the tags `rename` gives a new name.
TagRenamePlan planTags(const QString &content, const TagRenamer &rename)
{
    TagRenamePlan plan;
    const auto plan1 = [&](const NoteTag &t) {
        const QString name = rename(t.name);
        if (name.isNull() || name == t.name)
            return;
        const QString replacement = tagReplacement(t, name, content);
        if (!replacement.isEmpty() && QStringView(content).sliced(t.from, t.to - t.from) != replacement)
            plan.edits.append(TagEdit{t, replacement});
    };
    QList<NoteTag> tags;
    detail::parseNoteWith(QString(), content, VaultConfig(), StructureOptions(), &tags);
    std::stable_sort(tags.begin(), tags.end(), [](const NoteTag &a, const NoteTag &b) { return a.from < b.from; });
    for (const NoteTag &t : std::as_const(tags))
        plan1(t);
    plan.frontmatter = frontmatterTagItems(content);
    if (plan.frontmatter.key.isEmpty())
        return plan;
    plan.frontmatterBefore = namesFrom(parseFrontmatter(content).data.value(plan.frontmatter.key), true);
    plan.frontmatterAfter = plan.frontmatterBefore;
    if (plan.frontmatter.complete) {
        // The items are namesFrom()'s names one to one.
        for (qsizetype i = 0; i < plan.frontmatter.items.size(); ++i) {
            const FrontmatterTagItem &item = plan.frontmatter.items[i];
            const QString name = rename(item.tag.name);
            if (item.link || name.isNull() || name == item.tag.name)
                continue;
            const qsizetype before = plan.edits.size();
            plan1(item.tag);
            if (plan.edits.size() > before)
                plan.frontmatterAfter[i] = plan.edits.last().replacement;
        }
    } else {
        for (QString &n : plan.frontmatterAfter) {
            const QString name = rename(n.normalized(QString::NormalizationForm_C));
            if (!name.isNull() && name != n && !tagSyntax(name).isEmpty()) {
                n = name;
                ++plan.unplacedFrontmatter;
            }
        }
    }
    std::stable_sort(plan.edits.begin(), plan.edits.end(),
                     [](const TagEdit &a, const TagEdit &b) { return a.tag.from < b.tag.from; });
    return plan;
}

TagRenamePlan planTagRename(const QString &content, const QString &oldName, const QString &newName,
                            const VaultPath &oldPath, const VaultPath &newPath, const TagFilter &naming)
{
    const QString oldTrimmed = oldName.trimmed();
    const QString newTrimmed = newName.trimmed();
    if (newTrimmed.isEmpty() || (oldTrimmed.isEmpty() && oldPath.isEmpty()))
        return {};
    RenameSpec spec = makeSpec(oldTrimmed, newTrimmed, oldPath, newPath);
    if (oldTrimmed.isEmpty())
        spec.oldName = newTrimmed; // only path-qualified tags (no base-name match below)
    const QString oldKey = oldTrimmed.isEmpty() ? QString() : key(oldTrimmed);
    return planTags(content, [&](const QString &written) { return renamedTagName(written, spec, oldKey, naming); });
}

/// The text of the planned tag edits applied to `content` (and the frontmatter key rewritten
/// when its items cannot say the new names).
QString applyTagPlan(const QString &content, const TagRenamePlan &plan)
{
    QString out;
    qsizetype last = 0;
    for (const TagEdit &e : plan.edits) {
        if (e.tag.from < last)
            continue;
        out += QStringView(content).sliced(last, e.tag.from - last);
        out += e.replacement;
        last = e.tag.to;
    }
    out += QStringView(content).sliced(last);
    if (plan.frontmatterAfter != plan.frontmatterBefore) {
        // The YAML must read the new names; when the edited items do not (a name with a comma or
        // a space in a plain scalar list, …), the key is written anew.
        const QStringList now = namesFrom(parseFrontmatter(out).data.value(plan.frontmatter.key), true);
        if (now != plan.frontmatterAfter)
            out = setFrontmatterValue(out, plan.frontmatter.key, plan.frontmatterAfter);
    }
    return out == content ? content : out;
}

} // namespace

QString renameTags(const QString &content, const QString &oldName, const QString &newName, const VaultPath &oldPath,
                   const VaultPath &newPath, const TagFilter &naming)
{
    return applyTagPlan(content, planTagRename(content, oldName, newName, oldPath, newPath, naming));
}

QString retargetTags(const QString &content, const QHash<QString, QString> &names)
{
    if (names.isEmpty())
        return content;
    return applyTagPlan(content, planTags(content, [&names](const QString &written) {
                            QString k = key(written);
                            while (k.endsWith(u'/'))
                                k.chop(1);
                            const auto it = names.constFind(k);
                            return it == names.cend() ? QString() : it->trimmed();
                        }));
}

QList<NoteTag> tagsToRename(const QString &content, const QString &oldName, const QString &newName,
                            const VaultPath &oldPath, const VaultPath &newPath, const TagFilter &naming)
{
    const TagRenamePlan plan = planTagRename(content, oldName, newName, oldPath, newPath, naming);
    QList<NoteTag> out;
    for (const TagEdit &e : plan.edits)
        out.append(e.tag);
    // Frontmatter names the scan could not place: counted, without a range.
    for (int i = 0; i < plan.unplacedFrontmatter; ++i)
        out.append(NoteTag{QString(), -1, -1, 0, TagForm::Frontmatter});
    return out;
}

QString renameLinks(const QString &content, const QString &oldName, const QString &newName, const VaultPath &oldPath,
                    const VaultPath &newPath)
{
    if (oldName.trimmed().isEmpty() || newName.trimmed().isEmpty())
        return content; // never write empty link targets
    if (oldName == newName && (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath))
        return content;
    const RenameSpec spec = makeSpec(oldName, newName, oldPath, newPath);
    const ParsedNote note = parseNote(QString(), content);
    QString out;
    qsizetype last = 0;
    bool changed = false;
    for (const Link &link : note.links) {
        if (link.from < last)
            continue;
        const QStringView src = QStringView(content).sliced(link.from, link.to - link.from);
        QString rewritten;
        if (link.kind == LinkKind::Wiki || link.kind == LinkKind::Embed)
            rewritten = rewriteWiki(src, spec);
        else if (link.kind == LinkKind::Markdown)
            rewritten = rewriteMarkdown(src, link, spec);
        if (rewritten.isNull() || rewritten == src)
            continue;
        out += QStringView(content).sliced(last, link.from - last);
        out += rewritten;
        last = link.to;
        changed = true;
    }
    if (!changed)
        return content;
    out += QStringView(content).sliced(last);
    return out;
}

QString retargetLinks(const QString &content, const QList<LinkEdit> &edits, const QList<Link> *links)
{
    if (edits.isEmpty())
        return content;
    // The edits by range; only ranges of links the parser reports are rewritten.
    QHash<qsizetype, const LinkEdit *> byStart;
    for (const LinkEdit &e : edits) {
        if (e.from >= 0 && e.to > e.from && e.to <= content.size())
            byStart.insert(e.from, &e);
    }
    const auto validWiki = [](const QString &t) {
        return !t.trimmed().isEmpty() && !t.contains(u"[["_s) && !t.contains(u"]]"_s) && !t.contains(u'|')
               && !t.contains(u'#') && !t.contains(u'\n') && !t.contains(u'\r');
    };
    std::optional<ParsedNote> parsed;
    if (!links) {
        parsed = parseNote(QString(), content);
        links = &parsed->links;
    }
    QString out;
    qsizetype last = 0;
    bool changed = false;
    for (const Link &link : *links) {
        const LinkEdit *edit = byStart.value(link.from);
        if (!edit || edit->to != link.to || link.from < last)
            continue;
        const QStringView src = QStringView(content).sliced(link.from, link.to - link.from);
        QString rewritten;
        if (link.kind == LinkKind::Wiki || link.kind == LinkKind::Embed) {
            if (!validWiki(edit->newTarget))
                continue;
            const qsizetype open = src.startsWith(u'!') ? 3 : 2;
            const QStringView inner = src.sliced(open, src.size() - open - 2);
            qsizetype end = inner.size();
            const qsizetype hash = inner.indexOf(u'#');
            const qsizetype pipe = inner.indexOf(u'|');
            if (hash >= 0)
                end = hash;
            if (pipe >= 0 && pipe < end)
                end = pipe > 0 && inner[pipe - 1] == u'\\' ? pipe - 1 : pipe;
            const QStringView targetPart = inner.first(end);
            qsizetype lead = 0;
            while (lead < targetPart.size() && targetPart[lead].isSpace())
                ++lead;
            qsizetype trail = 0;
            while (trail < targetPart.size() - lead && targetPart[targetPart.size() - 1 - trail].isSpace())
                ++trail;
            rewritten = src.first(open).toString() + targetPart.first(lead) + edit->newTarget + targetPart.last(trail)
                        + inner.sliced(end) + u"]]"_s;
        } else if (link.kind == LinkKind::Markdown) {
            const QString target = edit->newTarget.trimmed();
            if (target.isEmpty() || target.contains(u'\n') || target.contains(u'\r') || hasUrlScheme(link.target))
                continue;
            const auto parts = parseMdLink(src, src.startsWith(u'!') ? 1 : 0);
            if (!parts)
                continue;
            const QStringView dest = src.sliced(parts->destFrom, parts->destTo - parts->destFrom);
            const qsizetype hash = dest.indexOf(u'#');
            const QStringView fragment = hash < 0 ? QStringView() : dest.sliced(hash);
            const DestStyle style = destStyle(hash < 0 ? dest : dest.first(hash), parts->angle);
            rewritten = src.first(parts->destFrom).toString() + encodeDest(target, style) + fragment
                        + src.sliced(parts->destTo);
        } else {
            continue;
        }
        if (rewritten == src)
            continue;
        out += QStringView(content).sliced(last, link.from - last);
        out += rewritten;
        last = link.to;
        changed = true;
    }
    if (!changed)
        return content;
    out += QStringView(content).sliced(last);
    return out;
}

} // namespace brulion::markdown
