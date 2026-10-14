// Internal: sending one request through an HttpTransport with the guarantees the clients rely
// on — the callback runs exactly once, always from the event loop, only while the context
// object lives, and at the latest when the request's time limit expires.
#pragma once

#include "sync/http.h"

#include <QObject>

namespace brulion::sync::detail {

/// Send `request` through `transport`. `done` runs exactly once — never inside this call —
/// unless `context` is destroyed first. With request.timeoutMs > 0, a transport that has not
/// answered in time is replaced by a timed-out HttpResponse (status 0, timedOut); its late
/// answer is dropped.
void sendRequest(HttpTransport *transport, QObject *context, const HttpRequest &request, HttpCallback done);

} // namespace brulion::sync::detail
