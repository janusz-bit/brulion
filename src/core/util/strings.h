// Small decisions about text that many modules make the same way.
#pragma once

#include <QString>

namespace brulion::strings {

/// How much of `text` to keep when it is cut to at most `max` UTF-16 units: `max` (or all of it),
/// one less when that would end between the halves of a surrogate pair (an emoji); 0 for `max` ≤ 0.
qsizetype cutLength(QStringView text, qsizetype max);

/// `length` characters drawn from `alphabet` (uniformly, QRandomGenerator::global()); "" for a
/// length ≤ 0. Ids of tasks, blocks, drawings' elements: each module keeps its own alphabet.
QString randomText(QStringView alphabet, int length);

} // namespace brulion::strings
