// VaultIndex: the page-property keys and values in use (propertyKeys(), propertyValues()) — the
// suggestions of the "Właściwości" panel and the type an empty value takes from the notebook.
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
    for (auto it = note.frontmatter.constBegin(); it != note.frontmatter.constEnd(); ++it) {
        if (it.key() == markdown::NoteModeKey || it.key().trimmed().isEmpty())
            continue;
        out.append({it.key(), it.value(), markdown::PropertySource::Yaml});
        yamlKeys.insert(it.key().toLower());
    }
    for (auto it = note.properties.constBegin(); it != note.properties.constEnd(); ++it) {
        if (yamlKeys.contains(it.key()) || it.key().isEmpty())
            continue;
        if (it.key() == markdown::NoteModeKey && note.frontmatter.contains(it.key()))
            continue;
        out.append({it.key(), it.value(), markdown::PropertySource::Logseq});
    }
    return out;
}

/// Case-insensitive NFC key of a property name or value.
QString foldedKey(const QString &s)
{
    return nfc(s).toLower();
}

/// The spelling used most (ties: the first seen).
QString mostUsed(const QList<std::pair<QString, int>> &spellings)
{
    QString best;
    int bestCount = -1;
    for (const auto &[spelling, count] : spellings) {
        if (count > bestCount) {
            best = spelling;
            bestCount = count;
        }
    }
    return best;
}

void countSpelling(QList<std::pair<QString, int>> &spellings, const QString &spelling)
{
    for (auto &[s, n] : spellings) {
        if (s == spelling) {
            ++n;
            return;
        }
    }
    spellings.append({spelling, 1});
}

} // namespace

QList<PropertyKeyInfo> VaultIndex::propertyKeys() const
{
    d->cache.read();
    if (d->cache.propertyKeys)
        return *d->cache.propertyKeys;
    ++d->builds.propertyKeys;
    struct Acc {
        QList<std::pair<QString, int>> spellings;
        int count = 0;
        QHash<int, int> types; ///< PropertyType -> notes with a non-empty value of it
        QList<int> typeOrder;  ///< types in the order first seen (ties)
    };
    QHash<QString, Acc> acc;
    QStringList order; // folded keys as first seen
    for (const Private::Entry *e : d->sortedEntries()) {
        QSet<QString> seen; // a key written twice in one note (YAML and Logseq) counts once
        for (const NoteProperty &p : pagePropertiesOf(e->note)) {
            const QString k = foldedKey(p.key);
            if (seen.contains(k))
                continue;
            seen.insert(k);
            auto it = acc.find(k);
            if (it == acc.end()) {
                it = acc.insert(k, Acc());
                order.append(k);
            }
            countSpelling(it->spellings, p.key);
            ++it->count;
            const bool empty = markdown::propertyValueTexts(p.key, p.value, p.source).isEmpty();
            const markdown::PropertyType type =
                    markdown::inferPropertyType(p.key, p.value, p.source, markdown::PropertyType::Unsupported);
            if (!empty && type != markdown::PropertyType::Unsupported) {
                if (!it->types.contains(int(type)))
                    it->typeOrder.append(int(type));
                ++it->types[int(type)];
            }
        }
    }
    QList<PropertyKeyInfo> out;
    out.reserve(order.size());
    for (const QString &k : std::as_const(order)) {
        const Acc &a = acc[k];
        PropertyKeyInfo info;
        info.key = mostUsed(a.spellings);
        info.count = a.count;
        int best = -1;
        int bestCount = 0;
        for (int t : a.typeOrder) {
            if (a.types.value(t) > bestCount) {
                best = t;
                bestCount = a.types.value(t);
            }
        }
        if (best >= 0)
            info.type = markdown::propertyTypeName(markdown::PropertyType(best));
        out.append(info);
    }
    std::stable_sort(out.begin(), out.end(), [](const PropertyKeyInfo &a, const PropertyKeyInfo &b) {
        if (a.count != b.count)
            return a.count > b.count;
        return compareNames(a.key, b.key) < 0;
    });
    d->cache.propertyKeys = out;
    return out;
}

QList<PropertyValueCount> VaultIndex::propertyValues(const QString &key, const QString &prefix, int limit) const
{
    QList<PropertyValueCount> out;
    const QString k = foldedKey(key.trimmed());
    if (limit <= 0 || k.isEmpty())
        return out;
    d->cache.read();
    auto cached = d->cache.propertyValues.constFind(k);
    if (cached == d->cache.propertyValues.constEnd()) {
        struct Acc {
            QList<std::pair<QString, int>> spellings;
            int count = 0;
        };
        QHash<QString, Acc> acc;
        QStringList order;
        for (const Private::Entry *e : d->sortedEntries()) {
            QSet<QString> seen; // a value counts once per note
            for (const NoteProperty &p : pagePropertiesOf(e->note)) {
                if (foldedKey(p.key) != k)
                    continue;
                for (const QString &v : markdown::propertyValueTexts(p.key, p.value, p.source)) {
                    const QString vk = foldedKey(v);
                    if (v.isEmpty() || seen.contains(vk))
                        continue;
                    seen.insert(vk);
                    auto it = acc.find(vk);
                    if (it == acc.end()) {
                        it = acc.insert(vk, Acc());
                        order.append(vk);
                    }
                    countSpelling(it->spellings, v);
                    ++it->count;
                }
            }
        }
        QList<PropertyValueCount> all;
        all.reserve(order.size());
        for (const QString &vk : std::as_const(order))
            all.append({mostUsed(acc[vk].spellings), acc[vk].count});
        std::stable_sort(all.begin(), all.end(), [](const PropertyValueCount &a, const PropertyValueCount &b) {
            if (a.count != b.count)
                return a.count > b.count;
            return compareNames(a.value, b.value) < 0;
        });
        cached = d->cache.propertyValues.insert(k, all);
    }
    const QString typed = text::foldKey(prefix.trimmed());
    if (typed.isEmpty())
        return cached->mid(0, limit);
    QList<PropertyValueCount> contains;
    for (const PropertyValueCount &v : *cached) {
        const QString folded = text::foldKey(v.value);
        if (folded.startsWith(typed)) {
            out.append(v);
            if (out.size() >= limit)
                return out;
        } else if (folded.contains(typed)) {
            contains.append(v);
        }
    }
    for (const PropertyValueCount &v : std::as_const(contains)) {
        if (out.size() >= limit)
            break;
        out.append(v);
    }
    return out;
}

} // namespace brulion
