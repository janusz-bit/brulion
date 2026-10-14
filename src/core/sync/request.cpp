#include "sync/request_p.h"

#include "sync/util_p.h"

#include <QPointer>
#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::sync::detail {

namespace {

struct PendingRequest {
    QPointer<QObject> context;
    HttpCallback done;
    QPointer<QTimer> timer;
    bool finished = false;
    bool inSend = true;

    void finish(const HttpResponse &response)
    {
        if (finished)
            return;
        finished = true;
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
        if (!context)
            return;
        const HttpCallback callback = std::move(done);
        done = nullptr;
        callback(response);
    }
};

HttpResponse timeoutResponse(const HttpRequest &request)
{
    HttpResponse response;
    response.timedOut = true;
    // One multi-argument arg(): percent escapes in the URL ("%20") must not count as placeholders.
    response.networkError = u"%1 %2 timed out after %3 s"_s.arg(QString::fromLatin1(request.method),
                                                                request.url.toString(QUrl::RemoveUserInfo),
                                                                QString::number((qint64(request.timeoutMs) + 999) / 1000));
    return response;
}

} // namespace

void sendRequest(HttpTransport *transport, QObject *context, const HttpRequest &request, HttpCallback done)
{
    auto pending = std::make_shared<PendingRequest>();
    pending->context = context;
    pending->done = std::move(done);
    if (!transport) {
        post(context, [pending] {
            HttpResponse response;
            response.networkError = u"No HTTP transport"_s;
            pending->finish(response);
        });
        return;
    }
    if (request.timeoutMs > 0) {
        auto *timer = new QTimer(context);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, context,
                         [pending, response = timeoutResponse(request)] { pending->finish(response); });
        pending->timer = timer;
        timer->start(request.timeoutMs);
    }
    transport->send(request, [pending](const HttpResponse &response) {
        if (pending->inSend) {
            // A transport answering synchronously: deliver from the event loop all the same.
            if (pending->context)
                post(pending->context, [pending, response] { pending->finish(response); });
            return;
        }
        pending->finish(response);
    });
    pending->inSend = false;
}

} // namespace brulion::sync::detail
