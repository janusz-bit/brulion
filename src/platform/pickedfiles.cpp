#include "pickedfiles.h"

#include <limits>
#include <utility>

namespace brulion::platform {

PickCollector::PickCollector(qint64 maxBytes, Done done) : m_maxBytes(maxBytes), m_done(std::move(done)) {}

void PickCollector::closed(int count)
{
    if (m_finished || m_expected >= 0)
        return;
    m_expected = count < 0 ? 0 : count;
    finishIfComplete();
}

char *PickCollector::accept(quint64 size, const QString &name)
{
    if (m_finished)
        return nullptr;
    PickedFile &file = m_files.emplace_back();
    file.name = name;
    file.size = size > quint64(std::numeric_limits<qint64>::max()) ? std::numeric_limits<qint64>::max()
                                                                   : qint64(size);
    // QByteArray's limit too (2 GB on 32-bit wasm).
    const bool tooLarge = (m_maxBytes >= 0 && file.size > m_maxBytes)
        || file.size > qint64(std::numeric_limits<qsizetype>::max() / 2);
    if (tooLarge) {
        file.tooLarge = true;
        ++m_processed;
        finishIfComplete();
        return nullptr;
    }
    file.data.resize(qsizetype(file.size));
    // Never nullptr for a file that is read, also an empty one (nullptr means "skip it").
    return file.data.data();
}

void PickCollector::ready()
{
    if (m_finished)
        return;
    ++m_processed;
    finishIfComplete();
}

void PickCollector::finishIfComplete()
{
    if (m_finished || m_expected < 0 || m_processed < m_expected)
        return;
    m_finished = true;
    QList<PickedFile> files;
    files.reserve(qsizetype(m_files.size()));
    for (PickedFile &f : m_files)
        files.append(std::move(f));
    m_files.clear();
    if (m_done)
        m_done(files);
}

} // namespace brulion::platform
