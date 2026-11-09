// VaultIndex: the page-property keys and values in use (propertyKeys(), propertyValues()) — the
// suggestions of the "Properties" panel and the type an empty value takes from the notebook.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include "index/textutil_p.h"
#include "markdown/notemode.h"
#include "markdown/pageprops.h"
#include "text/text.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace brulion {

using namespace index_detail;
using namespace Qt::StringLiterals;
using markdown::PropertyType;

namespace {

/// One page property of a note as the index sees it: the key as written, its value as parseNote()
/// keeps it and where it is written.
struct NoteProperty {
    QString key;
    QVariant value;
    markdown::PropertySource source = markdown::PropertySource::Yaml;
};

/// The page properties of a note: the YAML frontmatter (as written, without the note mode key),
/// then the leading `key:: value` lines (ParsedNote::properties keys the YAML does not have).
QList<NoteProperty> pagePropertiesOf(const ParsedNote &note)
{
    QList<NoteProperty> out;
    QSet<QString> yamlKeys;
    for (auto [key, value] : note.frontmatter.asKeyValueRange()) {
        if (key == markdown::NoteModeKey || key.trimmed().isEmpty())
            continue;
        out.append({key, value, markdown::PropertySource::Yaml});
        yamlKeys.insert(key.toLower());
    }
    for (auto [key, value] : note.properties.asKeyValueRange()) {
        if (yamlKeys.contains(key) || key.isEmpty())
            continue;
        if (key == markdown::NoteModeKey && note.frontmatter.contains(key))
            continue;
        out.append({key, value, markdown::PropertySource::Logseq});
    }
    return out;
}

/// Case-insensitive NFC key of a property name or value.
QString foldedKey(const QString &s)
{
    return nfc(s).toLower();
}

/// How often each of a few values was counted, in the order first counted (a linear search).
template<typename T>
class Counts {
public:
    void add(const T &value)
    {
        ++m_total;
        const auto it = std::ranges::find(m_counts, value, &std::pair<T, int>::first);
        if (it == m_counts.end())
            m_counts.append({value, 1});
        else
            ++it->second;
    }
    /// How many were counted in all.
    int total() const { return m_total; }
    /// The value counted most often (ties: the first counted); `none` when nothing was.
    T mostCounted(T none = T()) const
    {
        const auto it = std::ranges::max_element(m_counts, {}, &std::pair<T, int>::second);
        return it == m_counts.end() ? none : it->first;
    }

private:
    QList<std::pair<T, int>> m_counts;
    int m_total = 0;
};

/// The values of the page property with the folded key `key` in `entries`, most used first (a
/// value counts once per note, under the spelling most notes use).
QList<PropertyValueCount> valueCounts(const QList<const index_detail::Entry *> &entries, const QString &key)
{
    QHash<QString, Counts<QString>> spellings; // folded value -> its spellings
    QStringList order;                         // folded values as first seen
    for (const index_detail::Entry *e : entries) {
        QSet<QString> seen; // a value counts once per note
        for (const QList<NoteProperty> props = pagePropertiesOf(e->note); const NoteProperty &p : props) {
            if (foldedKey(p.key) != key)
                continue;
            for (const QStringList values = markdown::propertyValueTexts(p.key, p.value, p.source);
                 const QString &v : values) {
                const QString vk = foldedKey(v);
                if (v.isEmpty() || seen.contains(vk))
                    continue;
                seen.insert(vk);
                auto it = spellings.find(vk);
                if (it == spellings.end()) {
                    it = spellings.insert(vk, {});
                    order.append(vk);
                }
                it->add(v);
            }
        }
    }
    QList<PropertyValueCount> out;
    out.reserve(order.size());
    for (const QString &vk : std::as_const(order)) {
        const Counts<QString> &s = *spellings.constFind(vk);
        out.append({s.mostCounted(), s.total()});
    }
    std::ranges::stable_sort(out, [](const PropertyValueCount &a, const PropertyValueCount &b) {
        if (a.count != b.count)
            return a.count > b.count;
        return compareNames(a.value, b.value) < 0;
    });
    return out;
}

} // namespace

QList<PropertyKeyInfo> VaultIndex::propertyKeys() const
{
    d->cache.read();
    if (d->cache.propertyKeys)
        return *d->cache.propertyKeys;
    ++d->builds.propertyKeys;
    struct Acc {
        Counts<QString> spellings;  ///< one per note with the key
        Counts<PropertyType> types; ///< one per note with a non-empty value of a supported type
    };
    QHash<QString, Acc> acc;
    QStringList order; // folded keys as first seen
    for (const Private::Entry *e : d->sortedEntries()) {
        QSet<QString> seen; // a key written twice in one note (YAML and Logseq) counts once
        for (const QList<NoteProperty> props = pagePropertiesOf(e->note); const NoteProperty &p : props) {
            const QString k = foldedKey(p.key);
            if (seen.contains(k))
                continue;
            seen.insert(k);
            auto it = acc.find(k);
            if (it == acc.end()) {
                it = acc.insert(k, Acc());
                order.append(k);
            }
            it->spellings.add(p.key);
            const PropertyType type = markdown::inferPropertyType(p.key, p.value, p.source, PropertyType::Unsupported);
            if (type != PropertyType::Unsupported && !markdown::propertyValueTexts(p.key, p.value, p.source).isEmpty())
                it->types.add(type);
        }
    }
    QList<PropertyKeyInfo> out;
    out.reserve(order.size());
    for (const QString &k : std::as_const(order)) {
        const Acc &a = *acc.constFind(k);
        const PropertyType type = a.types.mostCounted(PropertyType::Unsupported);
        out.append({.key = a.spellings.mostCounted(),
                    .count = a.spellings.total(),
                    .type = type == PropertyType::Unsupported ? QString() : markdown::propertyTypeName(type)});
    }
    std::ranges::stable_sort(out, [](const PropertyKeyInfo &a, const PropertyKeyInfo &b) {
        if (a.count != b.count)
            return a.count > b.count;
        return compareNames(a.key, b.key) < 0;
    });
    d->cache.propertyKeys = out;
    return out;
}

QList<PropertyValueCount> VaultIndex::propertyValues(const QString &key, const QString &prefix, int limit) const
{
    const QString k = foldedKey(key.trimmed());
    if (limit <= 0 || k.isEmpty())
        return {};
    d->cache.read();
    QHash<QString, QList<PropertyValueCount>> &cache = d->cache.propertyValues;
    if (!cache.contains(k))
        cache.insert(k, valueCounts(d->sortedEntries(), k));
    const QList<PropertyValueCount> &all = *cache.constFind(k);
    const QString typed = text::foldKey(prefix.trimmed());
    if (typed.isEmpty())
        return all.mid(0, limit);
    QList<PropertyValueCount> out;
    QList<PropertyValueCount> contains;
    for (const PropertyValueCount &v : all) {
        const QString folded = text::foldKey(v.value);
        if (folded.startsWith(typed)) {
            out.append(v);
            if (out.size() >= limit)
                return out;
        } else if (folded.contains(typed)) {
            contains.append(v);
        }
    }
    out.append(contains.mid(0, limit - out.size()));
    return out;
}

} // namespace brulion
