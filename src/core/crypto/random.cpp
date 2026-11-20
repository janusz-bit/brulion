#include "crypto/random.h"

#include <QRandomGenerator>

#include <cstring>

namespace brulion::crypto {

RandomSource systemRandom()
{
    return [](char *out, qsizetype size) {
        QRandomGenerator *generator = QRandomGenerator::system();
        while (size >= qsizetype(sizeof(quint32))) {
            const quint32 word = generator->generate();
            std::memcpy(out, &word, sizeof word);
            out += sizeof word;
            size -= qsizetype(sizeof word);
        }
        if (size > 0) {
            const quint32 word = generator->generate();
            std::memcpy(out, &word, std::size_t(size));
        }
    };
}

QByteArray randomBytes(qsizetype size, const RandomSource &source)
{
    QByteArray out(qMax<qsizetype>(size, 0), Qt::Uninitialized);
    if (out.isEmpty())
        return out;
    if (source)
        source(out.data(), out.size());
    else
        systemRandom()(out.data(), out.size());
    return out;
}

} // namespace brulion::crypto
