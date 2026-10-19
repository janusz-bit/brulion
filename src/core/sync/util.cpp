#include "sync/util_p.h"

#include <QMetaObject>
#include <QStringDecoder>

namespace brulion::sync::detail {

std::optional<QString> decodeUtf8Strict(QByteArrayView bytes, bool stripBom)
{
    QStringDecoder::Flags flags = QStringDecoder::Flag::Stateless;
    if (!stripBom)
        flags |= QStringDecoder::Flag::ConvertInitialBom;
    QStringDecoder decoder(QStringDecoder::Utf8, flags);
    QString text = decoder.decode(bytes);
    if (decoder.hasError())
        return std::nullopt;
    return text;
}

void post(QObject *context, std::function<void()> fn)
{
    QMetaObject::invokeMethod(context, std::move(fn), Qt::QueuedConnection);
}

} // namespace brulion::sync::detail
