#include "attachments/attachmentsearch.h"

#include "text/text.h"
#include "util/paths.h"

#include <algorithm>

namespace brulion::attachments {

using namespace Qt::StringLiterals;

namespace {

/// The most documents one search ranks before grouping them by file (every page of a thousand
/// lecture PDFs matching "the" would be far beyond).
constexpr int MaxRanked = 20000;
/// Occurrences of one term looked at for an excerpt.
constexpr int MaxOccurrences = 200;

/// A character of a search token (SearchIndex's: a letter or a number), surrogate pairs whole.
bool isTokenAt(QStringView s, qsizetype i, qsizetype *length = nullptr)
{
    char32_t cp = s.at(i).unicode();
    qsizetype len = 1;
    if (QChar::isHighSurrogate(cp) && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
        cp = QChar::surrogateToUcs4(s.at(i), s.at(i + 1));
        len = 2;
    }
    if (length)
        *length = len;
    return QChar::isLetterOrNumber(cp);
}

bool tokenBefore(QStringView s, qsizetype i)
{
    if (i <= 0)
        return false;
    qsizetype start = i - 1;
    if (start > 0 && s.at(start).isLowSurrogate() && s.at(start - 1).isHighSurrogate())
        --start;
    return isTokenAt(s, start);
}

/// Where `term` (folded) occurs in `folded` as a whole token, at most `max` places.
QList<qsizetype> tokenOccurrences(QStringView folded, QStringView term, int max)
{
    QList<qsizetype> out;
    if (term.isEmpty())
        return out;
    for (qsizetype at = folded.indexOf(term); at >= 0 && out.size() < max; at = folded.indexOf(term, at + 1)) {
        const qsizetype end = at + term.size();
        if (!tokenBefore(folded, at) && (end >= folded.size() || !isTokenAt(folded, end)))
            out.append(at);
    }
    return out;
}

/// Runs of white space (and control characters) as one space, trimmed; soft hyphens dropped.
QString squash(QStringView s)
{
    constexpr char16_t SoftHyphen = 0x00AD;
    QString out;
    out.reserve(s.size());
    bool space = false;
    for (const QChar c : s) {
        if (c.unicode() == SoftHyphen)
            continue;
        if (c.isSpace() || c.category() == QChar::Other_Control) {
            space = true;
            continue;
        }
        if (space && !out.isEmpty())
            out.append(u' ');
        space = false;
        out.append(c);
    }
    return out;
}

/// The file's name without its extension: "Wykład 3" of "assets/Wykład 3.pdf".
QString nameOf(const QString &path)
{
    const QString base = paths::basename(path);
    const qsizetype dot = base.lastIndexOf(u'.');
    return dot > 0 ? base.first(dot) : base;
}

} // namespace

QString AttachmentSearch::docId(int file, int page)
{
    return QString::number(file) + u':' + QString::number(page);
}

std::pair<int, int> AttachmentSearch::fromDocId(const QString &id)
{
    const qsizetype colon = id.indexOf(u':');
    if (colon <= 0)
        return {0, 0};
    return {QStringView(id).first(colon).toInt(), QStringView(id).sliced(colon + 1).toInt()};
}

AttachmentSearch::PreparedPage AttachmentSearch::preparePage(int page, const Page &text)
{
    PreparedPage out;
    out.page = page;
    out.ocr = text.source == PageSource::Ocr;
    out.document = SearchIndex::prepare({}, {}, text.text);
    return out;
}

AttachmentSearch::Prepared AttachmentSearch::prepare(const QString &path, const Record &record)
{
    Prepared out;
    out.path = path;
    out.kind = record.kind;
    out.pageCount = record.pageCount;
    out.name = SearchIndex::prepare(nameOf(path), {}, {});
    for (qsizetype i = 0; i < record.pages.size(); ++i) {
        const Page &page = record.pages.at(i);
        if (page.text.trimmed().isEmpty())
            continue;
        out.pages.append(preparePage(int(i + 1), page));
    }
    return out;
}

void AttachmentSearch::removeDocs(const File &file)
{
    m_index.remove(docId(file.id, 0));
    for (const int page : file.pages)
        m_index.remove(docId(file.id, page));
}

void AttachmentSearch::set(Prepared prepared)
{
    if (prepared.path.isEmpty())
        return;
    remove(prepared.path);
    File file;
    file.id = m_nextId++;
    file.kind = prepared.kind;
    file.pageCount = prepared.pageCount;
    m_index.set(docId(file.id, 0), std::move(prepared.name));
    for (PreparedPage &page : prepared.pages) {
        if (page.page < 1 || page.document.terms.isEmpty())
            continue;
        file.pages.append(page.page);
        if (page.ocr)
            file.ocrPages.append(page.page);
        m_index.set(docId(file.id, page.page), std::move(page.document));
    }
    m_pathOf.insert(file.id, prepared.path);
    m_files.insert(prepared.path, std::move(file));
}

void AttachmentSearch::setPage(const QString &path, PreparedPage page)
{
    const auto it = m_files.find(path);
    if (it == m_files.end() || page.page < 1)
        return;
    File &file = *it;
    const QString id = docId(file.id, page.page);
    file.pages.removeAll(page.page);
    file.ocrPages.removeAll(page.page);
    if (page.document.terms.isEmpty()) {
        m_index.remove(id);
        return;
    }
    file.pages.insert(std::ranges::lower_bound(file.pages, page.page), page.page);
    if (page.ocr)
        file.ocrPages.insert(std::ranges::lower_bound(file.ocrPages, page.page), page.page);
    m_index.set(id, std::move(page.document));
}

void AttachmentSearch::remove(const QString &path)
{
    const auto it = m_files.constFind(path);
    if (it == m_files.constEnd())
        return;
    removeDocs(*it);
    m_pathOf.remove(it->id);
    m_files.erase(it);
}

void AttachmentSearch::rename(const QString &from, const QString &to)
{
    if (from == to)
        return;
    const auto it = m_files.constFind(from);
    if (it == m_files.constEnd())
        return;
    remove(to);
    File file = *it;
    m_files.erase(it);
    m_index.set(docId(file.id, 0), SearchIndex::prepare(nameOf(to), {}, {}));
    m_pathOf.insert(file.id, to);
    m_files.insert(to, std::move(file));
}

void AttachmentSearch::clear()
{
    m_index.clear();
    m_files.clear();
    m_pathOf.clear();
}

qsizetype AttachmentSearch::pageCount() const
{
    qsizetype n = 0;
    for (const File &file : m_files)
        n += file.pages.size();
    return n;
}

int AttachmentSearch::indexedPages(const QString &path) const
{
    const auto it = m_files.constFind(path);
    return it == m_files.constEnd() ? 0 : int(it->pages.size());
}

QList<AttachmentSearch::Hit> AttachmentSearch::search(QStringView query, int limit, Scopes kinds,
                                                      const PageText &pageText) const
{
    QList<Hit> out;
    if (limit <= 0 || m_index.size() == 0 || query.trimmed().isEmpty())
        return out;
    const bool pdfs = kinds.testFlag(Scope::Pdf);
    const bool pictures = kinds.testFlag(Scope::Pictures);
    if (!pdfs && !pictures)
        return out;
    QHash<int, qsizetype> at; // file id → its hit in `out`
    const int ranked = int(std::min<qsizetype>(m_index.size(), MaxRanked));
    for (const SearchIndex::Hit &raw : m_index.search(query, ranked)) {
        const auto [id, page] = fromDocId(raw.id);
        const auto known = at.constFind(id);
        if (known != at.constEnd()) {
            Hit &hit = out[*known];
            if (page > 0) {
                ++hit.matchedPages;
                if (hit.nameOnly) {
                    // The name matched best: the best page with the words then.
                    hit.nameOnly = false;
                    hit.page = page;
                    hit.terms = raw.terms;
                }
            }
            continue;
        }
        if (out.size() >= limit)
            continue; // (the pages of files already listed are still counted)
        const QString path = m_pathOf.value(id);
        const auto file = m_files.constFind(path);
        if (path.isEmpty() || file == m_files.constEnd())
            continue;
        if ((file->kind == Kind::Pdf && !pdfs) || (file->kind == Kind::Picture && !pictures))
            continue;
        Hit hit;
        hit.path = path;
        hit.kind = file->kind;
        hit.pageCount = file->pageCount;
        hit.score = raw.score;
        hit.literal = raw.literal;
        hit.terms = raw.terms;
        hit.nameOnly = page == 0;
        hit.page = std::max(1, page);
        hit.matchedPages = page > 0 ? 1 : 0;
        at.insert(id, out.size());
        out.append(std::move(hit));
    }
    for (Hit &hit : out) {
        const File &file = m_files[hit.path];
        hit.ocr = !hit.nameOnly && file.ocrPages.contains(hit.page);
        if (!pageText)
            continue;
        const std::optional<Page> page = pageText(hit.path, hit.page);
        if (!page)
            continue;
        hit.excerpt = excerpt(page->text, hit.nameOnly ? QStringList() : hit.terms);
        if (!hit.nameOnly)
            hit.find = findText(page->text, query, hit.terms);
    }
    return out;
}

QString AttachmentSearch::excerpt(QStringView text, const QStringList &terms, qsizetype max)
{
    if (text.isEmpty())
        return {};
    max = std::max<qsizetype>(max, 20);
    const text::FoldedText folded = text::foldKeyWithMap(text);
    // Every place of every term (folded offsets), with the term's number.
    QList<std::pair<qsizetype, int>> places;
    for (qsizetype t = 0; t < terms.size(); ++t) {
        for (const qsizetype pos : tokenOccurrences(folded.text, terms.at(t), MaxOccurrences))
            places.append({pos, int(t)});
    }
    std::ranges::sort(places);
    qsizetype from = 0;
    qsizetype to = std::min(text.size(), max);
    if (!places.isEmpty()) {
        // The place with the most distinct terms within half the excerpt after it.
        qsizetype best = places.constFirst().first;
        qsizetype bestTerm = places.constFirst().second;
        int bestCount = 0;
        for (qsizetype i = 0; i < places.size(); ++i) {
            QList<int> seen;
            for (qsizetype j = i; j < places.size() && places.at(j).first - places.at(i).first <= max / 2; ++j) {
                if (!seen.contains(places.at(j).second))
                    seen.append(places.at(j).second);
            }
            if (seen.size() > bestCount) {
                bestCount = int(seen.size());
                best = places.at(i).first;
                bestTerm = places.at(i).second;
            }
        }
        const qsizetype start = folded.toOriginal(best);
        const qsizetype end = folded.toOriginal(best + terms.at(bestTerm).size());
        // A third of the room before the hit.
        from = std::max<qsizetype>(0, start - (max - (end - start)) / 3);
        to = std::min(text.size(), from + max);
        from = std::max<qsizetype>(0, std::min(from, to - max));
    }
    // Whole words at both ends (within a few characters).
    if (from > 0) {
        qsizetype k = from;
        while (k < text.size() && k - from < 16 && !text.at(k - 1).isSpace())
            ++k;
        if (k - from < 16)
            from = k;
    }
    if (to < text.size()) {
        qsizetype k = to;
        while (k > from && to - k < 16 && !text.at(k).isSpace())
            --k;
        if (to - k < 16 && k > from)
            to = k;
    }
    QString out = squash(text.sliced(from, to - from));
    if (from > 0)
        out.prepend(u"… "_s);
    if (to < text.size() && !squash(text.sliced(to)).isEmpty())
        out.append(u" …"_s);
    return out;
}

QString AttachmentSearch::findText(QStringView text, QStringView query, const QStringList &terms)
{
    const QString folded = text::foldKey(text);
    const QString wanted = squash(text::foldKey(query));
    // Several words in a row on the page: as typed. (One word: the page's own spelling below.)
    if (wanted.contains(u' ') && squash(folded).contains(wanted))
        return squash(query);
    if (folded.size() == text.size()) {
        // (Folding kept every unit in place: the page's own spelling at the same offsets.)
        for (const QString &term : terms) {
            const QList<qsizetype> at = tokenOccurrences(folded, term, 1);
            if (!at.isEmpty())
                return text.sliced(at.constFirst(), term.size()).toString();
        }
    } else {
        const text::FoldedText mapped = text::foldKeyWithMap(text);
        for (const QString &term : terms) {
            const QList<qsizetype> at = tokenOccurrences(mapped.text, term, 1);
            if (!at.isEmpty()) {
                const qsizetype start = mapped.toOriginal(at.constFirst());
                return text.sliced(start, mapped.toOriginal(at.constFirst() + term.size()) - start).toString();
            }
        }
    }
    return terms.isEmpty() ? squash(query) : terms.constFirst();
}

} // namespace brulion::attachments
