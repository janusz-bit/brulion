// A report of an AI response (docs/UI.md "AI chat": Report response): the user flags an answer of
// the AI as offensive, sexual, violent, harassing, false or something else, and the report goes to
// Brulion's developers — Brulion has no server of its own, so it is a link the user opens and sends
// there: a new issue of the repository on GitHub (public), or an email when the build names an
// address (the CMake cache variable BRULION_AI_REPORT_EMAIL). Pure functions; nothing is sent.
//
//   aireport::Report r;
//   r.reason = aireport::Reason::Offensive;
//   r.response = u"…the AI's answer, as the user left it…"_s;
//   r.where = aireport::place(u"chat");       // "AI chat"
//   r.model = u"Brulion's AI assistant · Anthropic · claude-opus-5-5"_s;
//   r.version = u"0.28.1"_s;
//   r.device = u"Android 15, arm64"_s;
//   const aireport::Link link = aireport::link(r, BRULION_AI_REPORT_EMAIL); // the issue, or mail
//   QDesktopServices::openUrl(link.url);                                      // the user sends it
//
// The report is written in English (it goes to the developers, as the issues of the repository
// are), the user's and the AI's texts each in a code fence, literally: a mention, a link or HTML
// in them never turns into one on GitHub. The question before the answer goes along only when the
// user adds it (`question`: notes may be private). A link longer than `maxLength` gets the long
// texts cut — each to the same length, the shorter ones whole — and says so under each one cut.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>
#include <QUrl>

#include <optional>

namespace brulion::ai {
struct Settings;
}

namespace brulion::aireport {

/// Why the user reports the response.
enum class Reason { Offensive, Sexual, Violent, Harassment, Misleading, Other };

/// The reasons in the order the dialog lists them.
const QList<Reason> &reasons();
/// "offensive", "sexual", "violent", "harassment", "misleading", "other".
QString reasonId(Reason reason);
/// The reason of an id (any case); none for an unknown one.
std::optional<Reason> reasonFromId(QStringView id);
/// The reason as the report says it: "Offensive or hateful", "Sexual content", "Violent or
/// dangerous", "Harassment", "False or misleading", "Other".
QString reasonText(Reason reason);

/// What is reported.
struct Report {
    Reason reason = Reason::Other;
    QString comment;  ///< the user's own words ("" none)
    QString response; ///< the AI's text, as the user left it in the preview
    QString question; ///< the question before it, only when the user added it ("" none)
    QString where;    ///< what showed it: place() ("AI chat")
    QString model;    ///< who answered, as the app knows it ("Brulion's AI assistant · Anthropic · …")
    QString version;  ///< the app's ("0.28.1"; "0.28.1 (Google Play)")
    QString device;   ///< platform::deviceDescription() ("Android 15, arm64"): no user or host names
};

/// How long a link may be: GitHub refuses addresses of some 8 KB, mail apps differ.
inline constexpr int MaxLinkLength = 7000;

/// Where Brulion's issues are: "https://github.com/janusz-bit/brulion".
QString repositoryUrl();

/// "AI response report: Offensive or hateful".
QString title(const Report &report);

/// The report's text (Markdown): what it is, a list of the reason, where, the model, the app and
/// the device, then the comment, the response and the question (when there are) each fenced.
/// `limit` >= 0 cuts each of the three to at most that many characters (never inside a surrogate
/// pair), "…" after it, and a line under it says how much of it is there.
QString body(const Report &report, int limit = -1);

/// `text` in a code fence of backticks longer than any run of them in it, so nothing in it closes
/// the fence ("```" at least); "\n" between the lines.
QString fenced(QStringView text);

/// A link that opens the report, with the title and the text it holds (cut to fit: `cut`).
struct Link {
    QUrl url;
    QString title;
    QString body;
    bool cut = false;
    bool email = false; ///< a mailto: link (else a new issue on GitHub)
};

/// A new issue of the repository: …/issues/new?title=…&body=… (every character but letters,
/// digits and "-._~" percent-encoded, a space as %20, "+" as %2B), the texts cut so the link is
/// at most `maxLength` characters.
Link issueLink(const Report &report, int maxLength = MaxLinkLength);

/// An email to `address`: mailto:address?subject=…&body=… (RFC 6068: line breaks as %0D%0A), cut
/// as issueLink() cuts.
Link emailLink(const Report &report, QStringView address, int maxLength = MaxLinkLength);

/// emailLink() when `address` is an email address (isEmailAddress()), else issueLink().
Link link(const Report &report, QStringView address, int maxLength = MaxLinkLength);

/// A plain email address, `name@example.com` (letters, digits and ._%+- before the @, a domain of
/// labels with a dot): nothing that would change the mailto: link's meaning.
bool isEmailAddress(QStringView address);

/// What showed the response, from the app's code for it: "chat" → "AI chat", "writing/rewrite/
/// shorter" → "Writing assistant: rewrite (shorter)", "writing/summarize" → "Writing assistant:
/// summarize"; an unknown code → "AI response". Only [a-z0-9-] of a code's parts are kept.
QString place(QStringView code);

/// The model the settings name, as a report says it: "Anthropic · claude-opus-5-5" (the provider's
/// default when none is set), "OpenAI-compatible API · qwen3" — never its address, which may name a
/// computer at home —, "On this device · LFM3-VL 2B".
QString modelDescription(const ai::Settings &settings);

} // namespace brulion::aireport
