// The MCP methods of brulion-cli for both transports — standard input/output (`mcp`, mcp.cpp) and
// Streamable HTTP (`mcp serve`, mcpremote.cpp): one JSON-RPC message in, its answer out. Both eras
// of the protocol: the handshake versions (initialize, up to 2025-11-25) and the per-request ones
// (2026-07-28: `_meta` on every request, server/discover, no sessions).
#pragma once

#include "cli_p.h"

#include <QElapsedTimer>

#include <optional>

namespace brulion::cli {

/// What a caller may use.
struct McpPolicy {
    bool readOnly = false;
    bool allowDestructive = false;
    bool appControl = false; ///< the tools that drive the running app (never over the network)
    bool remote = false;     ///< over the network: no app.* tools at all, no paths of this computer
};

/// JSON-RPC and MCP error codes.
enum McpError : int {
    McpParseError = -32700,
    McpInvalidRequest = -32600,
    McpMethodNotFound = -32601,
    McpInvalidParams = -32602,
    McpInternalError = -32603,
    McpHeaderMismatch = -32020,
    McpMissingCapability = -32021,
    McpUnsupportedVersion = -32022,
};

class McpDispatcher {
    Q_DECLARE_TR_FUNCTIONS(brulion::cli)
public:
    explicit McpDispatcher(Context &context);

    /// The handshake (initialize) versions, newest first: the first is offered for an unknown one.
    static const QStringList &legacyVersions();
    /// The per-request versions, newest first.
    static const QStringList &modernVersions();
    /// All of them, newest first.
    static QStringList allVersions();
    static constexpr auto kProtocolVersionKey = "io.modelcontextprotocol/protocolVersion";

    struct Outcome {
        std::optional<QJsonObject> response; ///< none for a notification
        int httpStatus = 200;                ///< for HTTP: 400, 404 … where the modern protocol says so
        QString negotiated;                  ///< initialize: the version agreed
        bool modern = false;                 ///< a per-request (2026-07-28) request
    };

    /// Is the request (an object) one of the per-request protocol (`params._meta` names a
    /// protocol version)?
    static bool isModern(const QJsonObject &request);
    /// One JSON-RPC message, parsed.
    Outcome handle(const QJsonObject &request, const McpPolicy &policy);
    /// The command behind a tool name the policy offers, or nullptr.
    const Command *tool(const QString &name, const McpPolicy &policy) const;
    /// Is the command a tool under the policy?
    static bool offered(const Command &c, const McpPolicy &policy);

    static QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message,
                                     const QJsonValue &data = QJsonValue());
    static QJsonObject resultResponse(const QJsonValue &id, const QJsonObject &result);

private:
    QJsonObject callTool(const QJsonObject &params, const McpPolicy &policy, std::optional<QJsonObject> *error);
    QJsonArray toolList(const McpPolicy &policy) const;
    QString instructions(const McpPolicy &policy) const;
    QJsonObject serverInfo() const;

    Context &m_ctx;
    QElapsedTimer m_sinceRescan;
};

} // namespace brulion::cli
