#pragma once
// WorkspaceChats — the conversations with the AI of the open notebook (docs/UI.md "AI chat"; QML:
// Workspace.chats). A conversation is a page in the folder AI/ (core chat/chat.h: `- 🧑 …` and
// `- 🤖 …` messages, each with its time). Sending a message writes it into its page — a new
// conversation first gets its page, named after the question, and a line in today's journal that
// links to it — and then runs the turn of its partner (WorkspaceExtensions::runChat: the built-in
// assistant's command "chat", or an extension's chat command the page's `partner:` names) with the
// conversation's messages — whole turns from the oldest left out when they do not fit the model
// (chat::firstSent, historyBudget) — and the page it is about; the answer is written into the page
// as the AI's message. Pictures go with a message as attachments of the notebook embedded in it
// (addPicture…: stored when picked, dropped again when taken off before sending).
//
// Every conversation has a turn of its own (ChatTurn, turn(path)): several answer at once, one
// turn per conversation. The model on the device answers one turn at a time: the others wait in
// line (ChatTurn.queued, its place in line), and start in the order they were sent. A turn goes
// on whatever is on screen: the chat view may close, the answer still lands in its page (which
// follows a rename; a page deleted meanwhile ends the turn).
//
//   Workspace.chats.send("", "Co mam jutro do zrobienia?", "Dom")   // a new one: its path
//   Workspace.chats.send(path, "A pojutrze?")                         // the next message
//   Workspace.chats.turn(path).running / queued / status / partial / thoughts / failure
//   Workspace.chats.stop(path) / retry(path) / dismissFailure(path)
//   Workspace.chats.rename(path, "Nowa nazwa") / remove(path)        // the list's menu
//
// Single-threaded (the GUI thread).

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <utility>

namespace brulion::shell {

class Workspace;
class WorkspaceExtensions;
class WorkspaceChats;

/// The turn of one conversation (WorkspaceChats::turn): whether it runs or waits for the model
/// on the device, what it does, the answer as the model writes it, why it failed.
class ChatTurn : public QObject {
    Q_OBJECT
    /// The conversation (its page now: a rename moves it).
    Q_PROPERTY(QString path READ path NOTIFY stateChanged FINAL)
    /// A turn runs — or waits in line for the model on the device (queued).
    Q_PROPERTY(bool running READ running NOTIFY stateChanged FINAL)
    /// It waits for the model on the device, which answers another conversation; `place` in line
    /// (1: next).
    Q_PROPERTY(bool queued READ queued NOTIFY stateChanged FINAL)
    Q_PROPERTY(int place READ place NOTIFY stateChanged FINAL)
    /// What it does now (b.status: "Czytam „Dom”…"), "" while the model thinks.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    /// Why its last turn failed ("" none): shown in the conversation, with Try again.
    Q_PROPERTY(QString failure READ failure NOTIFY stateChanged FINAL)
    /// Of the running turn, the earlier messages the model did not get (they do not fit it).
    Q_PROPERTY(int omitted READ omitted NOTIFY stateChanged FINAL)
    /// The answer as the model writes it ("" none yet: it thinks, or calls a tool); each request
    /// of the turn starts anew. Markdown, the model's words.
    Q_PROPERTY(QString partial READ partial NOTIFY partialChanged FINAL)
    /// `partial` cut where its block being written starts (WorkspaceChats::splitWritten()): its
    /// finished blocks, which change only when a block is finished, and that block.
    Q_PROPERTY(QString partialSettled READ partialSettled NOTIFY settledChanged FINAL)
    Q_PROPERTY(QString partialTail READ partialTail NOTIFY partialChanged FINAL)
    /// The end of what the model thinks meanwhile, a few lines ("" none), and all of it (shown
    /// when the thoughts are opened).
    Q_PROPERTY(QString thoughts READ thoughts NOTIFY partialChanged FINAL)
    Q_PROPERTY(QString allThoughts READ allThoughts NOTIFY partialChanged FINAL)
    /// How fast the model writes now ("16,7 tok/s"); "" when not known.
    Q_PROPERTY(QString speed READ speed NOTIFY partialChanged FINAL)

public:
    explicit ChatTurn(const QString &path, QObject *parent = nullptr);

    QString path() const { return m_path; }
    bool running() const { return m_running; }
    bool queued() const { return m_queued; }
    int place() const { return m_place; }
    QString status() const { return m_status; }
    QString failure() const { return m_failure; }
    int omitted() const { return m_omitted; }
    QString partial() const { return m_partial; }
    QString partialSettled() const { return m_settled; }
    QString partialTail() const { return m_tail; }
    QString thoughts() const { return m_thoughts; }
    QString allThoughts() const { return m_allThoughts; }
    QString speed() const { return m_speed; }

Q_SIGNALS:
    void stateChanged();
    /// partial or thoughts changed (at most ten times a second).
    void partialChanged();
    /// partialSettled changed: a block of the answer was finished.
    void settledChanged();

private:
    friend class WorkspaceChats;
    void setPartial(const QString &text, const QString &thoughts, const QString &speed);
    void clearPartial();

    QString m_path;
    QString m_key; // the path the running turn was started with (its answer comes with it)
    bool m_running = false;
    bool m_queued = false;
    int m_place = 0;
    QString m_status;
    QString m_failure;
    int m_omitted = 0;
    QString m_partial;
    QString m_settled;
    QString m_tail;
    QString m_thoughts;
    QString m_allThoughts;
    QString m_speed;
    QString m_nextPartial; // what comes, said by m_partialTimer
    QString m_nextThoughts;
    QList<std::pair<qint64, int>> m_samples; // the request's {writing ms, tokens}, for its speed
    QTimer m_partialTimer;
};

class WorkspaceChats : public QObject {
    Q_OBJECT
    /// How many conversations' turns run or wait now.
    Q_PROPERTY(int runningCount READ runningCount NOTIFY stateChanged FINAL)
    /// Every running turn is on screen in a chat view (isShown): what the AI does is shown
    /// there, so the top bar's ✨ need not spin (no frame drawn for it).
    Q_PROPERTY(bool runningShown READ runningShown NOTIFY runningShownChanged FINAL)
    /// The characters of a conversation the model gets at most (chat::firstSent's budget): the
    /// model on the device holds little, a provider on the network a lot.
    Q_PROPERTY(int historyBudget READ historyBudget NOTIFY settingsChanged FINAL)
    /// The model takes pictures (not the model on the device that reads text only).
    Q_PROPERTY(bool seesPictures READ seesPictures NOTIFY settingsChanged FINAL)
    /// The model is the one on the device (its turns wait in line for it).
    Q_PROPERTY(bool onDevice READ onDevice NOTIFY settingsChanged FINAL)
    /// A photo can be taken for a message (Android's camera app; platform/capture.h).
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable CONSTANT FINAL)
    /// A message can be dictated (the system's speech recognizer, or one of the app's own).
    Q_PROPERTY(bool dictationAvailable READ dictationAvailable NOTIFY dictationChanged FINAL)
    /// Dictation listens now.
    Q_PROPERTY(bool listening READ listening NOTIFY dictationChanged FINAL)

public:
    WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent = nullptr);

    /// The characters of a conversation a model on the network gets at most (the request's own
    /// limit is ai::MaxRequestChars, with the tools and their results) …
    static constexpr int NetworkBudget = 120000;
    /// … and the model on the device (LocalAi::ContextTokens, some three characters a token,
    /// with room for the assistant's instructions, tools, their results and the answer).
    static constexpr int DeviceBudget = 16000;

    int runningCount() const;
    bool runningShown() const { return m_runningShown; }
    int historyBudget() const;
    bool seesPictures() const;
    bool onDevice() const;

    /// `markdown` being written as {its finished blocks, the block being written}: cut before the
    /// last line that starts a block at the margin after a blank line, outside a code block
    /// ("" and all of it when there is none). Each part renders as it would within the whole.
    static std::pair<QString, QString> splitWritten(const QString &markdown);
    /// Tokens a second over the last `windowMs` of `samples` ({writing ms, tokens} in order; a
    /// window shorter than half of it: over all of them); 0 when they do not tell.
    static double tokensPerSecond(const QList<std::pair<qint64, int>> &samples, qint64 windowMs = 2000);
    /// The end of `thoughts` the chat shows: at most about `chars` characters, from a word on.
    static QString thoughtsTail(const QString &thoughts, int chars = 240);

    /// The turn of the conversation `path` (made when first asked; the same object after a
    /// rename); "": a new conversation's, which only fails (the assistant off, a page not saved).
    Q_INVOKABLE brulion::shell::ChatTurn *turn(const QString &path);
    /// The turn of `path` runs (or waits in line) / waits in line — without making a ChatTurn.
    bool isRunning(const QString &path) const;
    bool isQueued(const QString &path) const;

    /// Writes `text` and `pictures` (vault paths, addPicture…) into the conversation `path` and
    /// runs the AI's turn; "" `path`: a new conversation about the page `page` (its name as a link
    /// names it; "" for none) answered by `partner` ("" the built-in assistant). Returns the
    /// conversation's path, "" when nothing was sent: no text nor picture, a turn of it running,
    /// the assistant off (said in the turn's failure), no notebook, a page that could not be
    /// written.
    Q_INVOKABLE QString send(const QString &path, const QString &text, const QString &page = QString(),
                             const QStringList &pictures = QStringList(), const QString &partner = QString());
    /// The AI's turn again on the conversation as it is (its last message the user's): after a
    /// failure or a stop. False when it cannot run now.
    Q_INVOKABLE bool retry(const QString &path);
    /// Stops the turn of `path` (running or in line): nothing is written for it (the user's
    /// message stays).
    Q_INVOKABLE void stop(const QString &path);
    /// Stops every turn.
    Q_INVOKABLE void stopAll();
    /// Forgets the failure shown in `path`.
    Q_INVOKABLE void dismissFailure(const QString &path);
    /// The page `path` is a conversation: in the folder AI/ with a message.
    Q_INVOKABLE bool isConversation(const QString &path) const;
    /// The folder of the conversations ("AI").
    Q_INVOKABLE QString folder() const;
    /// A chat view shows the conversation `path` (shown true) or no longer does (false): a question
    /// of its turn is then answered there, not in the dialog (ShellExtensionQuestion). Counted: a
    /// tab and the side panel may both show it.
    Q_INVOKABLE void showing(const QString &path, bool shown);
    /// A chat view shows the conversation `path`.
    Q_INVOKABLE bool isShown(const QString &path) const;

    // ---- the list's menu ------------------------------------------------------------------------
    /// The conversation's name without its date ("Co mam jutro" of "AI/2026-11-15 Co mam jutro.md").
    Q_INVOKABLE QString nameOf(const QString &path) const;
    /// Renames the conversation to `name`, its date kept in front (links to it follow:
    /// Workspace::renamePage); its new path, "" when it could not be (no name, one in use).
    Q_INVOKABLE QString rename(const QString &path, const QString &name);
    /// Moves the conversation to the trash (its turn stops); the trash's path ("" on failure), for
    /// Undo (Workspace::restore).
    Q_INVOKABLE QString remove(const QString &path);

    // ---- who answers ----------------------------------------------------------------------------
    /// The conversation's partner (its page's `partner:`, "" the built-in assistant).
    Q_INVOKABLE QString partnerOf(const QString &path) const;
    /// Sets who answers the conversation from now on (its page's frontmatter; "" the assistant).
    Q_INVOKABLE bool setPartner(const QString &path, const QString &partner);

    // ---- pictures sent with a message -----------------------------------------------------------
    /// A picture picked from a file (a file dialog, a drop, a photo the camera took) stored as an
    /// attachment of the notebook for the next message: its vault path ("" when it is no picture
    /// or cannot be stored).
    Q_INVOKABLE QString addPicture(const QUrl &file);
    /// addPicture() of each: the vault paths of those that are pictures and could be stored.
    Q_INVOKABLE QStringList addPictures(const QList<QUrl> &files);
    /// `attached` with addPictures() of `files` after it (each path once).
    Q_INVOKABLE QStringList addPicturesTo(const QStringList &attached, const QList<QUrl> &files);
    /// The clipboard's picture stored so ("" none on it).
    Q_INVOKABLE QString addClipboardPicture();
    /// A picture added but taken off the message before it was sent: deleted again.
    Q_INVOKABLE void dropPicture(const QString &path);
    /// The address of a picture of the notebook to show (Workspace::assetUrl).
    Q_INVOKABLE QUrl pictureUrl(const QString &name, const QString &conversation) const;
    /// The vault path of a picture a message names ("" when the notebook has none).
    Q_INVOKABLE QString picturePath(const QString &name, const QString &conversation) const;

    // ---- the camera and dictation ---------------------------------------------------------------
    bool cameraAvailable() const;
    bool dictationAvailable() const;
    bool listening() const;
    /// Asks the camera app for a photo: photoTaken(its vault path, stored as addPicture() stores a
    /// file) or captureFailed(why) follow (nothing when none was taken). False: no camera here.
    Q_INVOKABLE bool takePhoto();
    /// Starts listening, in the UI's language: dictated(text) when it is heard, dictationFailed(why)
    /// when not ("" cancelled). False when it cannot listen.
    Q_INVOKABLE bool startDictation();
    /// Ends listening: what was heard is given (dictated()).
    Q_INVOKABLE void stopDictation();
    /// Ends listening without a text.
    Q_INVOKABLE void cancelDictation();

    /// The notebook closed: no turn runs.
    void reset();

Q_SIGNALS:
    void stateChanged();
    /// A message was written into the conversation `path` (the view follows it to the end).
    void written(const QString &path);
    /// showing() changed what is shown.
    void shownChanged();
    void runningShownChanged();
    /// The model's settings changed (historyBudget, seesPictures, onDevice).
    void settingsChanged();
    /// addPictures() got files that are no pictures (or could not be stored).
    void picturesRefused();
    void photoTaken(const QString &path);
    void captureFailed(const QString &why);
    void dictationChanged();
    /// What dictation heard so far (a recognizer that tells), and at the end.
    void dictationPartial(const QString &text);
    void dictated(const QString &text);
    void dictationFailed(const QString &why);

private:
    ChatTurn *find(const QString &path) const;
    bool runTurn(ChatTurn *turn);
    /// Starts the turn now (false: failed at once).
    bool startTurn(ChatTurn *turn);
    void startWaiting();
    void updatePlaces();
    void answered(const QString &key, const QString &text);
    void failed(const QString &key, const QString &error);
    void setFailure(ChatTurn *turn, const QString &failure);
    void endTurn(ChatTurn *turn);
    void updateRunningShown();

    Workspace *m_workspace = nullptr;
    WorkspaceExtensions *m_extensions = nullptr;
    QHash<QString, ChatTurn *> m_turns; // by path, lower case
    QHash<QString, QPointer<ChatTurn>> m_byKey; // the running turns by the path they started with
    QList<QPointer<ChatTurn>> m_waiting; // in line for the model on the device, in order
    QPointer<ChatTurn> m_onDevice; // the turn the model on the device answers
    QHash<QString, int> m_shown; // by path, lower case: the chat views showing it
    QStringList m_added; // pictures added and not sent yet (dropPicture deletes only those)
    bool m_runningShown = false;
    QPointer<QObject> m_speech; // the recognizer connected to (platform::SpeechRecognizer)
    /// Connects the recognizer of now (it may come later: one of the app's own).
    void connectSpeech();
};

} // namespace brulion::shell
