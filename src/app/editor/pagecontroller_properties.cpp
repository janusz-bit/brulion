// PageController: the page's properties as typed fields (docs/UI.md "Page properties") — the
// `pageProperties` description for the properties panel and the page header, the single-property
// edits (markdown/pageprops.h) as undo steps, and the key / value suggestions.
#include "pagecontroller.h"

#include "blocklistmodel.h"
#include "blocktext.h"
#include "editorhost.h"

#include "markdown/notemode.h"
#include "markdown/properties.h"
#include "text/text.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::PropertyEditStatus;
using markdown::PropertySource;
using markdown::PropertyType;

namespace {

QString nfcLower(const QString &s)
{
    return s.normalized(QString::NormalizationForm_C).toLower();
}

/// Where property `key` of the page is (`source` when named), else where new ones go.
PropertySource sourceFor(const markdown::PageProperties &props, const QString &source, const QString &key)
{
    if (const auto named = markdown::parsePropertySource(source))
        return *named;
    for (const markdown::PageProperty &p : props.items) {
        if (p.source == PropertySource::Yaml ? p.key == key : nfcLower(p.key) == nfcLower(key))
            return p.source;
    }
    return props.addTarget();
}

/// A sentence for a field's error or a toast: why an edit was refused ("" when it was not).
QString reasonFor(PropertyEditStatus status, const QString &type, const QString &keyProblem)
{
    switch (status) {
    case PropertyEditStatus::Changed:
    case PropertyEditStatus::Unchanged: return {};
    case PropertyEditStatus::NotFound: return PageController::tr("This property was changed elsewhere");
    case PropertyEditStatus::Unsafe:
        return PageController::tr("The page's properties are not in a form Brulion can edit safely. Change them in "
                                  "the Markdown source (Ctrl+E).");
    case PropertyEditStatus::KeyExists: return PageController::tr("A property with this name exists");
    case PropertyEditStatus::Reserved:
        return PageController::tr("The note mode is switched with the button in the page header");
    case PropertyEditStatus::InvalidKey:
        if (keyProblem == u"logseqKey")
            return PageController::tr("Logseq-style keys (key:: value) may only have letters a–z, digits, - and _");
        if (keyProblem == u"lineBreak")
            return PageController::tr("A name cannot have line breaks");
        return PageController::tr("Enter a name");
    case PropertyEditStatus::InvalidValue:
        if (type == u"number")
            return PageController::tr("Not a number");
        if (type == u"date")
            return PageController::tr("Not a date");
        if (type == u"datetime")
            return PageController::tr("Not a date and time");
        if (type == u"checkbox")
            return PageController::tr("Neither yes nor no");
        if (type == u"aliases")
            return PageController::tr("An alias with this character cannot be used in a link");
        if (type == u"tags")
            return PageController::tr("Not a tag");
        return PageController::tr("This value cannot be written");
    }
    return {};
}

QVariantMap editResult(PropertyEditStatus status, const QString &reason)
{
    const bool ok = status == PropertyEditStatus::Changed || status == PropertyEditStatus::Unchanged;
    return {{u"ok"_s, ok}, {u"status"_s, markdown::propertyEditStatusName(status)}, {u"reason"_s, reason}};
}

/// The toast after a type change that could not keep the value.
QString lostReason(const QString &type, const QString &value)
{
    if (type == u"number")
        return PageController::tr("The value “%1” is not a number — it was cleared.").arg(value);
    if (type == u"checkbox")
        return PageController::tr("The value “%1” is neither yes nor no — it was cleared.").arg(value);
    if (type == u"date")
        return PageController::tr("The value “%1” is not a date — it was cleared.").arg(value);
    if (type == u"datetime")
        return PageController::tr("The value “%1” is not a date and time — it was cleared.").arg(value);
    return PageController::tr("The value “%1” could not be kept — it was cleared.").arg(value);
}

std::optional<PropertyType> listKind(const QString &kind)
{
    if (kind == u"tags")
        return PropertyType::Tags;
    if (kind == u"aliases")
        return PropertyType::Aliases;
    return std::nullopt;
}

} // namespace

void PageController::updatePageProperties()
{
    // The leading frontmatter rows (YAML, then `key::` lines) are the page's properties; in source
    // mode the source text (called after an edit made there and when the mode changes).
    QString text;
    const markdown::BlockDocument &doc = m_model->document();
    int rows = 0;
    if (m_sourceMode) {
        text = m_sourceText;
    } else {
        while (rows < doc.count() && doc.at(rows).kind == BlockKind::Frontmatter) {
            text += doc.at(rows).body + u'\n';
            ++rows;
        }
    }
    QVariantMap map = m_pageProperties;
    if (!m_pagePropertiesRead || text != m_pagePropertiesText) {
        m_pagePropertiesRead = true;
        m_pagePropertiesText = text;
        const markdown::PageProperties props = markdown::readPageProperties(text);
        QVariantList items;
        QVariantList tags;
        QVariantList aliases;
        QSet<QString> seenTags;
        QSet<QString> seenAliases;
        for (const markdown::PageProperty &p : props.items) {
            items.append(pagePropertyMap(p));
            const auto fixed = markdown::fixedPropertyType(p.key);
            if (!fixed || p.type != *fixed)
                continue;
            QVariantList &list = *fixed == PropertyType::Tags ? tags : aliases;
            QSet<QString> &seen = *fixed == PropertyType::Tags ? seenTags : seenAliases;
            for (const QString &name : p.items) {
                if (seen.contains(nfcLower(name)))
                    continue;
                seen.insert(nfcLower(name));
                list.append(QVariantMap{{u"name"_s, name},
                                        {u"source"_s, markdown::propertySourceName(p.source)},
                                        {u"property"_s, p.key},
                                        {u"editable"_s, p.editable}});
            }
        }
        map = {{u"items"_s, items},
               {u"tags"_s, tags},
               {u"aliases"_s, aliases},
               {u"canAdd"_s, props.canAdd()},
               {u"addTarget"_s, markdown::propertySourceName(props.addTarget())},
               {u"hasYaml"_s, props.yaml.has_value()},
               {u"hasLogseq"_s, props.logseq.has_value()},
               {u"editable"_s, !props.yaml || props.yaml->editable},
               {u"problem"_s, props.yaml ? props.yaml->problem : QString()}};
    }
    int visibleRow = -1;
    for (int i = 0; i < rows && !m_sourceMode; ++i) {
        if (m_model->isRowVisible(i)) {
            visibleRow = i;
            break;
        }
    }
    map.insert(u"visibleRow"_s, visibleRow);
    if (map == m_pageProperties)
        return;
    m_pageProperties = map;
    Q_EMIT pagePropertiesChanged();
}

PropertyEditStatus PageController::applyPropertyEdit(
        const std::function<markdown::PropertyEditResult(const QString &)> &edit)
{
    if (!m_loaded)
        return PropertyEditStatus::NotFound;
    const QString current = m_sourceMode ? m_sourceText : text();
    const markdown::PropertyEditResult r = edit(current);
    if (r.status != PropertyEditStatus::Changed)
        return r.status;
    if (m_sourceMode) {
        m_sourceText = r.content;
        m_sourceEdited = true;
        Q_EMIT sourceTextChanged();
        markDirty();
        updatePageProperties();
    } else {
        // One undo step; the block being edited (if any) keeps its caret: it follows its block.
        applyText(r.content, Focus{-1, -1});
    }
    resolveMode(false);
    return r.status;
}

QVariantMap PageController::setPageProperty(const QString &source, const QString &key, const QVariant &value,
                                            const QString &type)
{
    const QString k = key.trimmed();
    PropertySource resolved = PropertySource::Yaml;
    const PropertyEditStatus status = applyPropertyEdit([&](const QString &content) {
        resolved = sourceFor(markdown::readPageProperties(content), source, k);
        const PropertyType t = markdown::parsePropertyType(type).value_or(PropertyType::Text);
        return markdown::setPageProperty(content, resolved, k, value, t);
    });
    const auto fixed = markdown::fixedPropertyType(k);
    const QString t = fixed ? markdown::propertyTypeName(*fixed) : type;
    return editResult(status, reasonFor(status, t, markdown::checkPropertyKey(k, resolved)));
}

QVariantMap PageController::removePageProperty(const QString &source, const QString &key)
{
    const PropertyEditStatus status = applyPropertyEdit([&](const QString &content) {
        return markdown::removePageProperty(content, sourceFor(markdown::readPageProperties(content), source, key), key);
    });
    return editResult(status, reasonFor(status, {}, {}));
}

QVariantMap PageController::renamePageProperty(const QString &source, const QString &oldKey, const QString &newKey)
{
    const QString k = newKey.trimmed();
    PropertySource resolved = PropertySource::Yaml;
    const PropertyEditStatus status = applyPropertyEdit([&](const QString &content) {
        resolved = sourceFor(markdown::readPageProperties(content), source, oldKey);
        return markdown::renamePageProperty(content, resolved, oldKey, k);
    });
    return editResult(status, reasonFor(status, {}, markdown::checkPropertyKey(k, resolved)));
}

QVariantMap PageController::setPagePropertyType(const QString &source, const QString &key, const QString &type)
{
    const auto to = markdown::parsePropertyType(type);
    bool lost = false;
    QString previous;
    const PropertyEditStatus status = applyPropertyEdit([&](const QString &content) -> markdown::PropertyEditResult {
        const markdown::PageProperties props = markdown::readPageProperties(content);
        const markdown::PageProperty *p = props.find(sourceFor(props, source, key), key);
        if (!p)
            return {content, PropertyEditStatus::NotFound};
        if (!to || *to == PropertyType::Unsupported || markdown::fixedPropertyType(p->key))
            return {content, PropertyEditStatus::InvalidValue};
        previous = p->text;
        const auto converted = markdown::convertPropertyValue(p->value, p->type, *to);
        lost = !converted;
        return markdown::setPageProperty(content, p->source, p->key, converted.value_or(QVariant()), *to);
    });
    QVariantMap out = editResult(status, reasonFor(status, type, {}));
    lost = lost && out.value(u"ok"_s).toBool();
    out.insert(u"lost"_s, lost);
    if (lost)
        out.insert(u"reason"_s, lostReason(type, previous));
    return out;
}

QVariantMap PageController::addPageListItem(const QString &kind, const QString &item)
{
    const auto k = listKind(kind);
    if (!k)
        return editResult(PropertyEditStatus::InvalidValue, reasonFor(PropertyEditStatus::InvalidValue, kind, {}));
    const PropertyEditStatus status =
            applyPropertyEdit([&](const QString &content) { return markdown::addPageListItem(content, *k, item); });
    return editResult(status, reasonFor(status, kind, {}));
}

QVariantMap PageController::removePageListItem(const QString &kind, const QString &item)
{
    const auto k = listKind(kind);
    if (!k)
        return editResult(PropertyEditStatus::NotFound, reasonFor(PropertyEditStatus::NotFound, kind, {}));
    const PropertyEditStatus status =
            applyPropertyEdit([&](const QString &content) { return markdown::removePageListItem(content, *k, item); });
    return editResult(status, reasonFor(status, kind, {}));
}

QVariantList PageController::suggestPropertyKeys(const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_host || limit <= 0)
        return out;
    QSet<QString> own{nfcLower(markdown::NoteModeKey.toString())};
    for (const QVariant &v : m_pageProperties.value(u"items"_s).toList())
        own.insert(nfcLower(v.toMap().value(u"key"_s).toString()));
    const QString typed = text::foldKey(prefix.trimmed());
    QVariantList contains;
    for (const QVariant &v : m_host->propertyKeys()) {
        const QVariantMap k = v.toMap();
        const QString key = k.value(u"key"_s).toString();
        if (own.contains(nfcLower(key)))
            continue;
        const QString folded = text::foldKey(key);
        if (typed.isEmpty() || folded.startsWith(typed))
            out.append(k);
        else if (folded.contains(typed))
            contains.append(k);
        if (out.size() >= limit)
            return out;
    }
    for (const QVariant &k : std::as_const(contains)) {
        if (out.size() >= limit)
            break;
        out.append(k);
    }
    return out;
}

QVariantList PageController::suggestPropertyValues(const QString &key, const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_host || limit <= 0)
        return out;
    // What the property holds already is not offered again.
    QSet<QString> held;
    for (const QVariant &v : m_pageProperties.value(u"items"_s).toList()) {
        const QVariantMap p = v.toMap();
        if (nfcLower(p.value(u"key"_s).toString()) != nfcLower(key))
            continue;
        for (const QString &item : p.value(u"items"_s).toStringList())
            held.insert(nfcLower(item));
        if (p.value(u"items"_s).toStringList().isEmpty() && !p.value(u"empty"_s).toBool())
            held.insert(nfcLower(p.value(u"value"_s).toString()));
    }
    const auto add = [&](const QVariantMap &m) {
        if (out.size() < limit && !held.contains(nfcLower(m.value(u"value"_s).toString())))
            out.append(m);
    };
    const QString typed = prefix.trimmed();
    const auto fixed = markdown::fixedPropertyType(key);
    if (typed.startsWith(u"[[") && fixed != PropertyType::Tags && fixed != PropertyType::Aliases) {
        QString query = typed.mid(2);
        if (query.endsWith(u"]]"))
            query.chop(2);
        for (const QVariant &v : m_host->suggestPages(query, limit)) {
            const QVariantMap page = v.toMap();
            const QString path = page.value(u"path"_s).toString();
            const QString target = m_host->linkTarget(path, m_path);
            if (target.isEmpty())
                continue;
            add({{u"value"_s, QString(u"[["_s + target + u"]]"_s)},
                 {u"count"_s, 0},
                 {u"kind"_s, u"page"_s},
                 {u"title"_s, page.value(u"title"_s)},
                 {u"path"_s, path}});
        }
        return out;
    }
    if (fixed == PropertyType::Aliases)
        return out;
    if (fixed == PropertyType::Tags) {
        const QString tag = typed.startsWith(u'#') ? typed.mid(1) : typed;
        for (const QVariant &v : m_host->suggestTags(tag, limit + int(held.size()))) {
            const QVariantMap t = v.toMap();
            add({{u"value"_s, t.value(u"tag"_s)}, {u"count"_s, t.value(u"count"_s)}, {u"kind"_s, u"tag"_s}});
        }
        return out;
    }
    for (const QVariant &v : m_host->propertyValues(key, typed, limit + int(held.size()))) {
        const QVariantMap m = v.toMap();
        add({{u"value"_s, m.value(u"value"_s)}, {u"count"_s, m.value(u"count"_s)}, {u"kind"_s, u"value"_s}});
    }
    return out;
}

QString PageController::notebookPropertyType(const QString &key) const
{
    if (!m_host)
        return {};
    if (!m_notebookTypes) {
        QHash<QString, QString> types;
        for (const QVariant &v : m_host->propertyKeys()) {
            const QVariantMap k = v.toMap();
            types.insert(nfcLower(k.value(u"key"_s).toString()), k.value(u"type"_s).toString());
        }
        m_notebookTypes = types;
    }
    return m_notebookTypes->value(nfcLower(key.trimmed()));
}

} // namespace brulion::editor
