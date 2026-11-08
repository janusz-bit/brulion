#include "github/gitobjects.h"

#include <QCryptographicHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::github {

QString blobSha(QByteArrayView content)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData("blob ");
    hash.addData(QByteArray::number(content.size()));
    hash.addData(QByteArrayView("\0", 1));
    hash.addData(content);
    return QString::fromLatin1(hash.result().toHex());
}

bool isSha(QStringView text)
{
    return text.size() == 40 && std::all_of(text.begin(), text.end(), [](QChar c) {
               const char16_t u = c.unicode();
               return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F');
           });
}

QString shortSha(QStringView sha)
{
    return sha.first(std::min<qsizetype>(sha.size(), 7)).toString().toLower();
}

bool isLfsPointer(QByteArrayView content)
{
    // The spec: "version https://git-lfs.github.com/spec/v1" first, at most 1024 bytes; the
    // first pointers named hawser.github.com.
    return content.size() <= 1024
           && (content.startsWith("version https://git-lfs.github.com/spec/v1\n")
               || content.startsWith("version https://hawser.github.com/spec/v1\n"));
}

} // namespace brulion::github
