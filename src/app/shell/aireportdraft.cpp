#include "aireportdraft.h"

#include "platform.h"

#include "aireport/aireport.h"

using namespace Qt::StringLiterals;

// The address reports go to by email: the CMake cache variable BRULION_AI_REPORT_EMAIL (top-level
// CMakeLists.txt; "" — the default — a GitHub issue instead).
#ifndef BRULION_AI_REPORT_EMAIL
#error "BRULION_AI_REPORT_EMAIL comes from the CMake cache variable of that name (src/app/shell/area.cmake)"
#endif

namespace brulion::shell {

AiReportDraft::AiReportDraft(QObject *parent) : QObject(parent) { update(); }

QString AiReportDraft::buildAddress() { return QStringLiteral(BRULION_AI_REPORT_EMAIL).trimmed(); }

void AiReportDraft::setReason(const QString &reason)
{
    const std::optional<aireport::Reason> known = aireport::reasonFromId(reason);
    const QString id = known ? aireport::reasonId(*known) : QString();
    if (id == m_reason)
        return;
    m_reason = id;
    update();
}

void AiReportDraft::setComment(const QString &comment)
{
    if (comment == m_comment)
        return;
    m_comment = comment;
    update();
}

void AiReportDraft::setResponse(const QString &response)
{
    if (response == m_response)
        return;
    m_response = response;
    update();
}

void AiReportDraft::setQuestion(const QString &question)
{
    if (question == m_question)
        return;
    m_question = question;
    update();
}

void AiReportDraft::setIncludeQuestion(bool include)
{
    if (include == m_includeQuestion)
        return;
    m_includeQuestion = include;
    update();
}

void AiReportDraft::setWhere(const QString &where)
{
    if (where == m_where)
        return;
    m_where = where;
    update();
}

void AiReportDraft::setModel(const QString &model)
{
    if (model == m_model)
        return;
    m_model = model;
    update();
}

QStringList AiReportDraft::reasons() const
{
    QStringList ids;
    for (const aireport::Reason r : aireport::reasons())
        ids.append(aireport::reasonId(r));
    return ids;
}

QString AiReportDraft::version() const
{
    const QString version = QStringLiteral(BRULION_VERSION);
    return platform::playStoreBuild() ? version + u" (Google Play)"_s : version;
}

QString AiReportDraft::device() const { return platform::deviceDescription(); }

bool AiReportDraft::byEmail() const { return aireport::isEmailAddress(buildAddress()); }

QString AiReportDraft::destination() const
{
    return byEmail() ? buildAddress() : aireport::repositoryUrl().mid(QStringLiteral("https://").size());
}

QString AiReportDraft::reasonLabel(const QString &id) const
{
    const std::optional<aireport::Reason> reason = aireport::reasonFromId(id);
    if (!reason)
        return {};
    switch (*reason) {
    case aireport::Reason::Offensive: return tr("Offensive or hateful");
    case aireport::Reason::Sexual: return tr("Sexual content");
    case aireport::Reason::Violent: return tr("Violent or dangerous");
    case aireport::Reason::Harassment: return tr("Harassment");
    case aireport::Reason::Misleading: return tr("False or misleading");
    case aireport::Reason::Other: break;
    }
    return tr("Other");
}

void AiReportDraft::update()
{
    aireport::Report report;
    report.reason = aireport::reasonFromId(m_reason).value_or(aireport::Reason::Other);
    report.comment = m_comment;
    report.response = m_response;
    report.question = m_includeQuestion ? m_question : QString();
    report.where = aireport::place(m_where);
    report.model = m_model;
    report.version = version();
    report.device = device();
    const aireport::Link link = aireport::link(report, buildAddress());
    m_title = link.title;
    m_body = link.body;
    m_cut = link.cut;
    m_url = link.url;
    Q_EMIT changed();
}

} // namespace brulion::shell
