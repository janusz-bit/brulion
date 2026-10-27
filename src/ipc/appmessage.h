// The messages a second launch or brulion-cli sends to the running Brulion over the
// single-instance socket (ipc/singleinstance.h): one line of UTF-8 per message, a verb and an
// optional JSON object.
//
//   activate                          show the window (a second launch)
//   hidden                            nothing (a second launch with --hidden: Windows' start at
//                                     sign-in while Brulion already runs)
//   capture                           show the window and open quick capture
//   today                             show the window and open today's journal
//   open {"route": {...}, "vault": "/abs/path"}
//                                     show the window and navigate to the route (docs/UI.md
//                                     "Navigation": {"view": "page", "path": …, "line"?, "blockId"?},
//                                     {"view": "journal", "date": …}) once the notebook is ready;
//                                     a route for another notebook than the one open is refused
//                                     with a notice (brulion-cli app open)
//
// The channel is one-way (nothing is answered). Unknown verbs are ignored by the app
// (AppController::handleInstanceMessage: not even the window comes up), so newer senders stay
// harmless; the full remote control of the running app (state, navigate, screenshot, click,
// type …) will add request/response verbs on a socket of its own.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace brulion::ipc {

struct AppMessage {
    QByteArray verb;     ///< "activate", "capture", "today", "open", …
    QJsonObject payload; ///< {} when the line has none
};

/// One message line (without the line break): "verb" or "verb {compact json}".
QByteArray encodeMessage(const QByteArray &verb, const QJsonObject &payload = {});
/// Parse a received line; nullopt for an empty line, a verb with characters other than [a-z-], or
/// a payload that is not a JSON object.
std::optional<AppMessage> parseMessage(const QByteArray &line);

/// The `open` message for a route of the notebook at `vaultRoot`.
QByteArray openMessage(const QJsonObject &route, const QString &vaultRoot);

} // namespace brulion::ipc
