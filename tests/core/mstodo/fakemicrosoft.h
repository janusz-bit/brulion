// A fake Microsoft for the mstodo tests (and the app's MsTodoImport tests): the identity
// platform's device-code and token endpoints and the Microsoft Graph To Do API, answered from
// memory through sync::HttpTransport — asynchronously, like the real transport. No network.
// The data is synthetic, shaped like Graph's todoTaskList / todoTask documentation.
#pragma once

#include "sync/http.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace mstodotest {

class FakeMicrosoft : public brulion::sync::HttpTransport {
public:
    FakeMicrosoft();

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override;

    // ---- Sign-in -----------------------------------------------------------------------------
    QString clientId = QStringLiteral("3f2504e0-4f89-41d3-9a0c-0305e82c3301");
    QString accessToken = QStringLiteral("EwBwA8l6BAAUfakeAccessTokenZażółć1234567890");
    QString userCode = QStringLiteral("F7GH2K9QX");
    int interval = 5;
    int expiresIn = 900;
    /// Answers of the polls in order: "authorization_pending", "slow_down", "authorization_declined",
    /// "expired_token", "AADSTS7000218", "network" (no answer), …; then the token.
    QStringList pollScript{QStringLiteral("authorization_pending"), QStringLiteral("authorization_pending")};
    /// Error JSON for the device-code request ({} = a code is given); an unknown client id
    /// answers AADSTS700016 by itself.
    QJsonObject deviceCodeError;
    int polls = 0;

    // ---- Graph -------------------------------------------------------------------------------
    /// The lists (todoTaskList objects with an "id").
    QList<QJsonObject> lists;
    /// list id -> its tasks (todoTask objects with an "id"; without checklistItems etc.)
    QHash<QString, QList<QJsonObject>> tasks;
    /// task id -> its steps / linked resources / attachments (taskFileAttachment, with
    /// contentBytes — the list request leaves them out unless attachmentListHasContent).
    QHash<QString, QJsonArray> checklistItems;
    QHash<QString, QJsonArray> linkedResources;
    QHash<QString, QJsonArray> attachments;
    int listPageSize = 1;
    int taskPageSize = 2;
    bool refuseExpand = false;
    bool attachmentListHasContent = false;
    /// The first `throttle` Graph requests are answered 429 with Retry-After: 1.
    int throttle = 0;
    /// Answer every Graph request with this status (0 = normally, -1 = no answer at all).
    int graphStatus = 0;
    /// The tasks' paging link of this list points to another host.
    QString foreignNextLinkList;

    // ---- What happened ------------------------------------------------------------------------
    QList<brulion::sync::HttpRequest> requests;
    int graphRequests() const;
    /// Every request's URL, headers and body, and every answer's body, as one text.
    QString transcript() const;

    /// Two lists ("Zadania" — the default list — and "Zakupy ąę") with tasks, steps, links and an
    /// attachment.
    void addSampleData();

private:
    brulion::sync::HttpResponse answer(const brulion::sync::HttpRequest &request);
    brulion::sync::HttpResponse login(const brulion::sync::HttpRequest &request);
    brulion::sync::HttpResponse graph(const brulion::sync::HttpRequest &request);
    QStringList m_answers;
};

} // namespace mstodotest
