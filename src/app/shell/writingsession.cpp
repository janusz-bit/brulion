#include "writingsession.h"

#include "workspace.h"
#include "workspaceextensions.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

WritingSession::WritingSession(QObject *parent)
    : QObject(parent)
{
    // The answer as it comes, shown ten times a second at most (as the chat's).
    m_partialTimer.setSingleShot(true);
    m_partialTimer.setInterval(100);
    connect(&m_partialTimer, &QTimer::timeout, this, [this] {
        if (m_state == u"waiting" || m_state == u"writing")
            setResult(writing::clean(m_raw, m_input, true));
    });
}

WritingSession::~WritingSession()
{
    m_pending.stop();
}

void WritingSession::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    clear();
    m_workspace = workspace;
    Q_EMIT workspaceChanged();
}

bool WritingSession::onDevice() const
{
    return m_workspace && m_workspace->extensions()->aiSettings().provider == u"local";
}

QString WritingSession::presetLabel(const QString &preset) const
{
    if (preset == u"shorter")
        return tr("Shorter");
    if (preset == u"clearer")
        return tr("Clearer");
    if (preset == u"grammar")
        return tr("Fix spelling and grammar");
    if (preset == u"formal")
        return tr("More formal");
    if (preset == u"casual")
        return tr("More casual");
    if (preset == u"english")
        return tr("Translate to English");
    if (preset == u"polish")
        return tr("Translate to Polish");
    return preset;
}

bool WritingSession::start(const QString &action, const QString &preset, const QString &instruction, const QString &text,
                           const QString &before, const QString &after, const QString &page, bool inlineText)
{
    stop();
    m_choosing = false;
    m_input = writing::Input();
    m_input.action = writing::actionFrom(action);
    m_input.text = text;
    m_input.before = before;
    m_input.after = after;
    m_input.page = page;
    m_input.inline_ = inlineText;
    m_preset = m_input.action == writing::Action::Rewrite ? preset : QString();
    m_instruction = m_preset.isEmpty() ? instruction.trimmed() : presetLabel(m_preset);
    m_input.instruction = m_preset.isEmpty() ? instruction.trimmed() : writing::presetInstruction(m_preset);
    return send();
}

bool WritingSession::begin(const QString &action, const QString &preset, const QString &text, const QString &before,
                           const QString &after, const QString &page, bool inlineText)
{
    if (writing::actionFrom(action) == writing::Action::Rewrite && preset.isEmpty()) {
        // "Rewrite…": the presets first, nothing asked yet.
        stop();
        m_input = writing::Input();
        m_input.text = text;
        m_input.before = before;
        m_input.after = after;
        m_input.page = page;
        m_input.inline_ = inlineText;
        m_preset.clear();
        m_instruction.clear();
        m_raw.clear();
        m_choosing = true;
        setResult(QString());
        setState(QString());
        Q_EMIT stateChanged();
        return true;
    }
    m_choosing = false;
    return start(action, preset, QString(), text, before, after, page, inlineText);
}

bool WritingSession::startInline(const QString &action, const QString &preset, int row, const QString &editText, int from,
                                 int to, const QString &page)
{
    const qsizetype length = editText.size();
    qsizetype a = std::clamp<qsizetype>(std::min(from, to), 0, length);
    qsizetype b = std::clamp<qsizetype>(std::max(from, to), 0, length);
    const bool more = writing::actionFrom(action) == writing::Action::Continue;
    if (more) {
        a = b;
    } else if (a == b) {
        a = 0;
        b = length;
    }
    m_target = u"inline"_s;
    m_row = row;
    m_firstRow = row;
    m_base = editText;
    m_from = a;
    m_to = b;
    return begin(action, preset, more ? editText.left(b) : editText.mid(a, b - a), more ? QString() : editText.left(a),
                 editText.mid(b), page, a > 0 || b < length);
}

bool WritingSession::startBlocks(const QString &action, const QString &preset, const QString &markdown, int firstRow,
                                 int lastRow, const QString &page)
{
    m_target = u"blocks"_s;
    m_row = lastRow;
    m_firstRow = firstRow;
    m_base = markdown;
    m_from = 0;
    m_to = markdown.size();
    return begin(action, preset, markdown, QString(), QString(), page, false);
}

bool WritingSession::startSource(const QString &action, const QString &preset, const QString &source, int from, int to,
                                 const QString &page)
{
    const qsizetype length = source.size();
    qsizetype a = std::clamp<qsizetype>(std::min(from, to), 0, length);
    qsizetype b = std::clamp<qsizetype>(std::max(from, to), 0, length);
    const bool more = writing::actionFrom(action) == writing::Action::Continue;
    if (a == b && !more) {
        // The paragraph at the caret: between blank lines.
        const qsizetype start = source.left(a).lastIndexOf(u"\n\n"_s);
        const qsizetype end = source.indexOf(u"\n\n"_s, a);
        a = start < 0 ? 0 : start + 2;
        b = end < 0 ? length : end;
    }
    if (more)
        a = b;
    m_target = u"source"_s;
    m_row = -1;
    m_firstRow = -1;
    m_base = source;
    m_from = a;
    m_to = b;
    return begin(action, preset, more ? source.left(b) : source.mid(a, b - a), more ? QString() : source.left(a),
                 source.mid(b), page, false);
}

QVariantMap WritingSession::change() const { return edit(m_base, int(m_from), int(m_to)); }

int WritingSession::changeFrom() const { return int(writing::edit(m_base, m_from, m_to, m_result, m_input.action).from); }

int WritingSession::changeTo() const { return int(writing::edit(m_base, m_from, m_to, m_result, m_input.action).to); }

QString WritingSession::changeText() const { return writing::edit(m_base, m_from, m_to, m_result, m_input.action).text; }

bool WritingSession::retry()
{
    if (m_input.text.isEmpty())
        return false;
    stop();
    return send();
}

bool WritingSession::rewriteWith(const QString &preset, const QString &instruction)
{
    if (m_input.text.isEmpty())
        return false;
    stop();
    m_choosing = false;
    m_input.action = writing::Action::Rewrite;
    m_preset = preset;
    m_instruction = preset.isEmpty() ? instruction.trimmed() : presetLabel(preset);
    m_input.instruction = preset.isEmpty() ? instruction.trimmed() : writing::presetInstruction(preset);
    return send();
}

bool WritingSession::send()
{
    m_raw.clear();
    m_partialTimer.stop();
    setResult(QString());
    if (!m_workspace || !m_workspace->isOpen()) {
        setState(u"failed"_s, tr("Open a notebook first."));
        return false;
    }
    if (const QString problem = writing::problem(m_input); !problem.isEmpty()) {
        setState(u"failed"_s, m_input.action == writing::Action::Rewrite && m_input.instruction.trimmed().isEmpty()
                                      ? tr("Choose how to rewrite it, or say it in your words.")
                              : m_input.text.size() > writing::MaxText
                                      ? tr("The text is too long for the AI: at most %1 characters.").arg(writing::MaxText)
                                      : tr("There is no text to work on: select some, or put the caret in a block."));
        return false;
    }
    WorkspaceExtensions *extensions = m_workspace->extensions();
    QString why;
    if (!extensions->aiSettings().usable(&why)) {
        setState(u"failed"_s, tr("Set up the AI first (Settings → Extensions → AI): %1").arg(why));
        return false;
    }
    const quint64 request = ++m_request;
    setState(u"waiting"_s);
    const QPointer<WritingSession> self(this);
    m_pending = extensions->sendAi(
            writing::request(m_input),
            [self, request](const ai::Reply &reply) {
                if (!self || request != self->m_request)
                    return; // (an older request's, stopped or asked again)
                self->m_partialTimer.stop();
                if (!reply.ok) {
                    if (reply.error == u"stopped")
                        return; // (stop() said so already)
                    self->setState(u"failed"_s, tr("AI: %1").arg(reply.error));
                    return;
                }
                const QString text = writing::clean(reply.text, self->m_input);
                if (text.isEmpty()) {
                    self->setState(u"failed"_s, tr("The AI gave no text: try again."));
                    return;
                }
                self->setResult(text);
                self->setState(u"done"_s);
            },
            [self, request](const ai::Partial &partial) {
                if (!self || request != self->m_request)
                    return;
                self->m_raw = partial.text;
                if (!partial.text.isEmpty() && self->m_state == u"waiting")
                    self->setState(u"writing"_s);
                if (!self->m_partialTimer.isActive())
                    self->m_partialTimer.start();
            });
    return true;
}

void WritingSession::stop()
{
    if (m_state != u"waiting" && m_state != u"writing")
        return;
    ++m_request; // (its answer, if one still comes, is dropped)
    m_pending.stop();
    m_pending = ai::Pending();
    m_partialTimer.stop();
    const QString kept = writing::clean(m_raw, m_input, true);
    setResult(kept);
    setState(kept.isEmpty() ? QString() : u"stopped"_s);
}

void WritingSession::clear()
{
    stop();
    m_choosing = false;
    m_target.clear();
    m_row = -1;
    m_firstRow = -1;
    m_base.clear();
    m_from = m_to = 0;
    m_input = writing::Input();
    m_preset.clear();
    m_instruction.clear();
    m_raw.clear();
    setResult(QString());
    setState(QString());
}

bool WritingSession::canAccept() const
{
    return !m_result.trimmed().isEmpty() && (m_state == u"done" || m_state == u"stopped");
}

void WritingSession::setState(const QString &state, const QString &error)
{
    if (m_state == state && m_error == error)
        return;
    m_state = state;
    m_error = error;
    Q_EMIT stateChanged();
}

void WritingSession::setResult(const QString &result)
{
    if (m_result == result)
        return;
    m_result = result;
    Q_EMIT resultChanged();
}

QString WritingSession::changesHtml(const QColor &removedColor, const QColor &addedBackground) const
{
    const auto css = [](const QColor &c) {
        return u"rgba(%1,%2,%3,%4)"_s.arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 2);
    };
    const auto escaped = [](const QString &text) {
        QString html = text.toHtmlEscaped();
        html.replace(u'\n', u"<br/>"_s);
        return html;
    };
    QString html;
    for (const writing::Change &c : writing::diff(m_input.text, m_result)) {
        switch (c.kind) {
        case writing::Change::Same:
            html += escaped(c.text);
            break;
        case writing::Change::Removed:
            html += u"<span style=\"color:%1; text-decoration:line-through;\">%2</span>"_s.arg(css(removedColor),
                                                                                                escaped(c.text));
            break;
        case writing::Change::Added:
            html += u"<span style=\"background-color:%1;\">%2</span>"_s.arg(css(addedBackground), escaped(c.text));
            break;
        }
    }
    return u"<p style=\"white-space:pre-wrap; margin:0px;\">"_s + html + u"</p>"_s;
}

QVariantMap WritingSession::edit(const QString &text, int from, int to) const
{
    const writing::Edit e = writing::edit(text, from, to, m_result, m_input.action);
    return {{u"from"_s, int(e.from)}, {u"to"_s, int(e.to)}, {u"text"_s, e.text}};
}

} // namespace brulion::shell
