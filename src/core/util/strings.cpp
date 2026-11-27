#include "util/strings.h"

#include <QRandomGenerator>

#include <algorithm>

namespace brulion::strings {

qsizetype cutLength(QStringView text, qsizetype max)
{
    if (text.size() <= max)
        return text.size();
    qsizetype keep = std::max<qsizetype>(max, 0);
    if (keep > 0 && text.at(keep - 1).isHighSurrogate())
        --keep;
    return keep;
}

QString randomText(QStringView alphabet, int length)
{
    QString out;
    if (alphabet.isEmpty())
        return out;
    QRandomGenerator *random = QRandomGenerator::global();
    for (int i = 0; i < length; ++i)
        out += alphabet.at(random->bounded(int(alphabet.size())));
    return out;
}

} // namespace brulion::strings
