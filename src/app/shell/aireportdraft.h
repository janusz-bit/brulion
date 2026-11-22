#pragma once
// AiReportDraft — the report of an AI response being written in the "Report response" dialog
// (docs/UI.md "AI chat"; QML: `AiReportDraft {}` in ShellAiReportDialog): what the user chose and
// typed — the reason, a comment, the response and the question as edited in the preview — and what
// the report then is (core aireport/aireport.h): its title and text as they will be sent, and the
// link that opens it — an email to the address the build names (the CMake cache variable
// BRULION_AI_REPORT_EMAIL), else a new public issue of Brulion on GitHub —, the texts cut to fit the
// link. The dialog opens the link with Workspace.openUrl; Brulion itself sends nothing.
//
//   draft.where = "chat"; draft.model = Workspace.chats.modelName(path)
//   draft.response = answer; draft.question = question          // the question only when added:
//   draft.includeQuestion = box.checked
//   draft.reason = "offensive"                                  // ready
//   Workspace.openUrl(draft.url)
//
// Single-threaded (the GUI thread).

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace brulion::shell {

class AiReportDraft : public QObject {
    Q_OBJECT
    /// The reason's id ("offensive", "sexual", "violent", "harassment", "misleading", "other");
    /// "" none chosen yet (`ready` false).
    Q_PROPERTY(QString reason READ reason WRITE setReason NOTIFY changed FINAL)
    Q_PROPERTY(QString comment READ comment WRITE setComment NOTIFY changed FINAL)
    /// The AI's text, as the user left it in the preview.
    Q_PROPERTY(QString response READ response WRITE setResponse NOTIFY changed FINAL)
    /// The question before it; goes along only with `includeQuestion`.
    Q_PROPERTY(QString question READ question WRITE setQuestion NOTIFY changed FINAL)
    Q_PROPERTY(bool includeQuestion READ includeQuestion WRITE setIncludeQuestion NOTIFY changed FINAL)
    /// What showed it: a code of aireport::place() ("chat", "writing/rewrite/shorter").
    Q_PROPERTY(QString where READ where WRITE setWhere NOTIFY changed FINAL)
    /// Who answered, as the app knows it (WorkspaceChats::modelName, WritingSession::modelName).
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY changed FINAL)
    /// The reasons' ids in the dialog's order.
    Q_PROPERTY(QStringList reasons READ reasons CONSTANT FINAL)
    /// The app's version ("0.28.1"; "0.28.1 (Google Play)") and the device ("Android 15, arm64").
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString device READ device CONSTANT FINAL)
    /// The report goes by email (the build names an address), else as a GitHub issue.
    Q_PROPERTY(bool byEmail READ byEmail CONSTANT FINAL)
    /// Where it goes: the email address, or "github.com/janusz-bit/brulion".
    Q_PROPERTY(QString destination READ destination CONSTANT FINAL)
    /// A reason is chosen: it can be sent.
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    /// The report as it will be sent: its title, its text (cut to fit the link: `cut`) and the
    /// link that opens it.
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString body READ body NOTIFY changed FINAL)
    Q_PROPERTY(bool cut READ cut NOTIFY changed FINAL)
    Q_PROPERTY(QUrl url READ url NOTIFY changed FINAL)

public:
    explicit AiReportDraft(QObject *parent = nullptr);

    QString reason() const { return m_reason; }
    void setReason(const QString &reason);
    QString comment() const { return m_comment; }
    void setComment(const QString &comment);
    QString response() const { return m_response; }
    void setResponse(const QString &response);
    QString question() const { return m_question; }
    void setQuestion(const QString &question);
    bool includeQuestion() const { return m_includeQuestion; }
    void setIncludeQuestion(bool include);
    QString where() const { return m_where; }
    void setWhere(const QString &where);
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QStringList reasons() const;
    QString version() const;
    QString device() const;
    bool byEmail() const;
    QString destination() const;
    bool ready() const { return !m_reason.isEmpty(); }
    QString title() const { return m_title; }
    QString body() const { return m_body; }
    bool cut() const { return m_cut; }
    QUrl url() const { return m_url; }

    /// A reason's name in the UI ("Offensive or hateful"; translated).
    Q_INVOKABLE QString reasonLabel(const QString &id) const;

    /// The address the build names (BRULION_AI_REPORT_EMAIL; "" none). For tests.
    static QString buildAddress();

Q_SIGNALS:
    void changed();

private:
    /// The report and its link again, from what is chosen now.
    void update();

    QString m_reason;
    QString m_comment;
    QString m_response;
    QString m_question;
    bool m_includeQuestion = false;
    QString m_where;
    QString m_model;
    QString m_title;
    QString m_body;
    bool m_cut = false;
    QUrl m_url;
};

} // namespace brulion::shell
