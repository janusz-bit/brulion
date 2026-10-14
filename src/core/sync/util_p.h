// Internal helpers shared by the sync module's translation units.
#pragma once

#include <QByteArrayView>
#include <QLoggingCategory>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::sync::detail {

Q_DECLARE_LOGGING_CATEGORY(lcSync)

/// Strict UTF-8 decoding: std::nullopt when `bytes` are not valid UTF-8. With `stripBom` a
/// leading byte order mark is dropped, otherwise it is kept as U+FEFF.
std::optional<QString> decodeUtf8Strict(QByteArrayView bytes, bool stripBom);

/// Run `fn` from `context`'s event loop (never synchronously); dropped when `context` dies.
void post(QObject *context, std::function<void()> fn);

} // namespace brulion::sync::detail
