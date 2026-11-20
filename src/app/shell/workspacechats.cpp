#include "workspacechats.h"

#include "workspace.h"
#include "workspaceextensions.h"

#include "capture.h"

#include "ai/local.h"
#include "chat/chat.h"
#include "log/categories.h"
#include "util/paths.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QMimeData>

using namespace Qt::StringLiterals;

namespace brulion::shell {

// ---- ChatTurn -------------------------------------------------------------------------------------

ChatTurn::ChatTurn(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    // The answer as it is written: shown ten times a second at most (a provider streams a word at
    // a time, every change renders the message anew, and a model on the device shares the
    // processor with that).
    m_partialTimer.setSingleShot(true);
    m_partialTimer.setInterval(100);
    connect(&m_partialTimer, &QTimer::timeout, this, [this] {
        const double rate = WorkspaceChats::tokensPerSecond(m_samples);
        setPartial(m_nextPartial, m_nextThoughts,
                   rate > 0 ? WorkspaceChats::tr("%1 tok/s").arg(QLocale().toString(rate, 'f', 1)) : QString());
    });
}

void ChatTurn::setPartial(const QString &text, const QString &thoughts, const QString &speed)
{
    const QString tail = WorkspaceChats::thoughtsTail(thoughts);
    const QString all = thoughts.trimmed();
    if (text == m_partial && tail == m_thoughts && all == m_allThoughts && speed == m_speed)
        return;
    const bool newText = text != m_partial;
    m_partial = text;
    m_thoughts = tail;
    m_allThoughts = all;
    m_speed = speed;
    if (newText) {
        auto [settled, rest] = WorkspaceChats::splitWritten(text);
        m_tail = rest;
        if (settled != m_settled) {
            m_settled = settled;
            Q_EMIT settledChanged();
        }
    }
    Q_EMIT partialChanged();
}

void ChatTurn::clearPartial()
{
    m_partialTimer.stop();
    m_samples.clear();
    m_nextPartial.clear();
    m_nextThoughts.clear();
    setPartial(QString(), QString(), QString());
}

// ---- WorkspaceChats -------------------------------------------------------------------------------

WorkspaceChats::WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent)
    : QObject(parent)
    , m_workspace(workspace)
    , m_extensions(extensions)
{
    connect(m_extensions, &WorkspaceExtensions::chatAnswered, this, &WorkspaceChats::answered);
    connect(m_extensions, &WorkspaceExtensions::chatFailed, this, &WorkspaceChats::failed);
    connect(m_extensions, &WorkspaceExtensions::chatStatus, this, [this](const QString &key, const QString &text) {
        ChatTurn *t = m_byKey.value(key);
        if (!t || text == t->m_status)
            return;
        t->m_status = text;
        Q_EMIT t->stateChanged();
    });
    connect(m_extensions, &WorkspaceExtensions::chatPartial, this,
            [this](const QString &key, const QString &text, const QString &thoughts, int tokens, qint64 writingMs) {
                ChatTurn *t = m_byKey.value(key);
                if (!t)
                    return;
                t->m_nextPartial = text;
                t->m_nextThoughts = thoughts;
                if (text.isEmpty() && thoughts.isEmpty() && tokens == 0) {
                    t->clearPartial(); // (a new request: at once)
                    return;
                }
                if (tokens > 0) {
                    t->m_samples.append({writingMs, tokens});
                    while (t->m_samples.size() > 200)
                        t->m_samples.removeFirst();
                }
                if (!t->m_partialTimer.isActive())
                    t->m_partialTimer.start();
            });
    connect(m_extensions, &WorkspaceExtensions::aiSettingsChanged, this, [this] {
        Q_EMIT settingsChanged();
        startWaiting(); // (a provider on the network answers every conversation at once)
    });
    // The page of a turn renamed or moved: the turn follows it (its answer goes where it is now);
    // deleted: the turn ends, nothing is written (a page made anew would hold the answer alone).
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        ChatTurn *t = m_turns.take(from.toLower());
        if (!t)
            return;
        if (ChatTurn *other = m_turns.take(to.toLower()); other && other != t)
            other->deleteLater();
        t->m_path = to;
        m_turns.insert(to.toLower(), t);
        Q_EMIT t->stateChanged();
        Q_EMIT stateChanged();
    });
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (ChatTurn *t = find(path); t && t->m_running)
            stop(path);
    });
    connect(this, &WorkspaceChats::stateChanged, this, &WorkspaceChats::updateRunningShown);
    connect(this, &WorkspaceChats::shownChanged, this, &WorkspaceChats::updateRunningShown);
}

double WorkspaceChats::tokensPerSecond(const QList<std::pair<qint64, int>> &samples, qint64 windowMs)
{
    if (samples.isEmpty())
        return 0;
    const auto &[lastMs, lastTokens] = samples.last();
    // The first sample within the window.
    qsizetype from = samples.size() - 1;
    while (from > 0 && lastMs - samples.at(from - 1).first <= windowMs)
        --from;
    const auto &[firstMs, firstTokens] = samples.at(from);
    if (lastMs - firstMs >= windowMs / 2 && lastTokens > firstTokens)
        return 1000.0 * (lastTokens - firstTokens) / double(lastMs - firstMs);
    // Too short a window yet: since the writing began.
    return lastMs > 0 ? 1000.0 * lastTokens / double(lastMs) : 0;
}

QString WorkspaceChats::thoughtsTail(const QString &thoughts, int chars)
{
    const QString text = thoughts.trimmed();
    if (text.size() <= chars)
        return text;
    qsizetype from = text.size() - chars;
    // From the next word on (a long word: where it is).
    const qsizetype space = text.indexOf(u' ', from);
    if (space >= 0 && space < text.size() - chars / 2)
        from = space + 1;
    if (text.at(from).isLowSurrogate())
        ++from;
    return u"…"_s + text.mid(from).trimmed();
}

std::pair<QString, QString> WorkspaceChats::splitWritten(const QString &markdown)
{
    qsizetype cut = -1;
    bool blank = false; // the line before was blank
    QString fence;      // the open code block's fence ("```", "~~~~"), "" outside one
    for (qsizetype at = 0; at < markdown.size();) {
        qsizetype end = markdown.indexOf(u'\n', at);
        if (end < 0)
            end = markdown.size();
        const QStringView line = QStringView(markdown).sliced(at, end - at);
        const QStringView trimmed = line.trimmed();
        // A fence: up to three spaces, then ``` or ~~~ (one closes only its own kind, as long).
        qsizetype indent = 0;
        while (indent < line.size() && indent < 4 && line.at(indent) == u' ')
            ++indent;
        const QStringView rest = line.sliced(indent);
        qsizetype run = 0;
        while (run < rest.size() && (rest.at(run) == u'`' || rest.at(run) == u'~') && rest.at(run) == rest.at(0))
            ++run;
        const bool isFence = indent < 4 && run >= 3;
        if (fence.isEmpty()) {
            if (blank && at > 0 && !line.isEmpty() && !line.at(0).isSpace())
                cut = at; // a block at the margin after a blank line
            if (isFence)
                fence = rest.left(run).toString();
        } else if (isFence && rest.at(0) == fence.at(0) && run >= fence.size() && rest.sliced(run).trimmed().isEmpty()) {
            fence.clear();
        }
        blank = trimmed.isEmpty();
        at = end + 1;
    }
    if (cut <= 0)
        return {QString(), markdown};
    QString settled = markdown.left(cut);
    while (!settled.isEmpty() && settled.back().isSpace())
        settled.chop(1);
    return {settled, markdown.mid(cut)};
}

int WorkspaceChats::runningCount() const
{
    int n = 0;
    for (const ChatTurn *t : m_turns)
        n += t->m_running ? 1 : 0;
    return n;
}

bool WorkspaceChats::onDevice() const { return m_extensions->aiSettings().provider == u"local"; }

int WorkspaceChats::historyBudget() const { return onDevice() ? DeviceBudget : NetworkBudget; }

bool WorkspaceChats::seesPictures() const
{
    const ai::Settings &settings = m_extensions->aiSettings();
    if (settings.provider != u"local")
        return true; // (Claude sees; a server's model that does not says so in its error)
    const ai::LocalModel *model = ai::localModel(settings.model.trimmed());
    return model && model->sees();
}

void WorkspaceChats::updateRunningShown()
{
    bool any = false, all = true;
    for (const ChatTurn *t : std::as_const(m_turns)) {
        if (!t->m_running)
            continue;
        any = true;
        all = all && isShown(t->m_path);
    }
    const bool shown = any && all;
    if (shown == m_runningShown)
        return;
    m_runningShown = shown;
    Q_EMIT runningShownChanged();
}

QString WorkspaceChats::folder() const { return chat::folder(); }

void WorkspaceChats::showing(const QString &path, bool shown)
{
    if (path.isEmpty())
        return;
    const QString key = path.toLower();
    const int n = std::max(0, m_shown.value(key) + (shown ? 1 : -1));
    if (n == 0)
        m_shown.remove(key);
    else
        m_shown.insert(key, n);
    Q_EMIT shownChanged();
}

bool WorkspaceChats::isShown(const QString &path) const { return !path.isEmpty() && m_shown.contains(path.toLower()); }

bool WorkspaceChats::isConversation(const QString &path) const
{
    if (!chat::inFolder(path) || !m_workspace->ready())
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    return ok && !chat::parse(content).messages.isEmpty();
}

ChatTurn *WorkspaceChats::find(const QString &path) const { return m_turns.value(path.toLower()); }

bool WorkspaceChats::isRunning(const QString &path) const
{
    const ChatTurn *t = find(path);
    return t && t->m_running;
}

bool WorkspaceChats::isQueued(const QString &path) const
{
    const ChatTurn *t = find(path);
    return t && t->m_queued;
}

ChatTurn *WorkspaceChats::turn(const QString &path)
{
    if (ChatTurn *t = find(path))
        return t;
    auto *t = new ChatTurn(path, this);
    m_turns.insert(path.toLower(), t);
    return t;
}

QString WorkspaceChats::send(const QString &path, const QString &text, const QString &page, const QStringList &pictures,
                             const QString &partner)
{
    const QString message = text.trimmed();
    QStringList sent;
    for (const QString &picture : pictures) {
        if (!picture.trimmed().isEmpty() && !sent.contains(picture.trimmed()))
            sent.append(picture.trimmed());
    }
    if ((message.isEmpty() && sent.isEmpty()) || !m_workspace->ready())
        return {};
    if (ChatTurn *t = path.isEmpty() ? nullptr : find(path); t && t->m_running)
        return {};
    const QString language = m_extensions->language();
    const QString who = path.isEmpty() ? partner.trimmed() : partnerOf(path);
    if (who.isEmpty() && !m_extensions->enabled().contains(u"asystent"_s)) {
        setFailure(turn(path), tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        return {};
    }
    const QString markdown = chat::withPictures(message, sent);
    const QDateTime now = m_workspace->now();
    QString target = path;
    if (target.isEmpty()) {
        // A new conversation: its page, named after the question, and a line in today's journal.
        const QString title = chat::pageTitle(message, now.date(), language);
        target = paths::uniquePath(title + u".md"_s, [this](const QString &p) { return m_workspace->exists(p); });
        if (!m_workspace->write(target, chat::newConversation(markdown, page, language, now, who))) {
            setFailure(turn(QString()), tr("The conversation could not be saved."));
            return {};
        }
        m_workspace->appendJournalBlocks(chat::journalEntry(target.chopped(3)));
        setFailure(turn(QString()), QString());
    } else {
        bool ok = false;
        const QString content = m_workspace->read(target, &ok);
        if (!ok || !m_workspace->write(target, chat::appendMessage(content, chat::Role::User, markdown, now, language))) {
            setFailure(turn(target), tr("The conversation could not be saved."));
            return {};
        }
    }
    for (const QString &picture : std::as_const(sent))
        m_added.removeAll(picture); // (part of the notebook now)
    Q_EMIT written(target);
    runTurn(turn(target));
    return target;
}

bool WorkspaceChats::retry(const QString &path)
{
    if (path.isEmpty() || !m_workspace->ready())
        return false;
    ChatTurn *t = turn(path);
    if (t->m_running)
        return false;
    return runTurn(t);
}

bool WorkspaceChats::runTurn(ChatTurn *t)
{
    bool ok = false;
    const QString content = m_workspace->read(t->m_path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    if (!ok || conversation.messages.isEmpty() || conversation.messages.last().role != chat::Role::User) {
        setFailure(t, tr("There is no question to answer: write one."));
        return false;
    }
    if (onDevice() && m_onDevice && m_onDevice != t) {
        // The model on the device answers another conversation: this one waits in line.
        t->m_running = true;
        t->m_queued = true;
        t->m_status.clear();
        t->m_failure.clear();
        t->clearPartial();
        m_waiting.append(t);
        updatePlaces();
        Q_EMIT t->stateChanged();
        Q_EMIT stateChanged();
        return true;
    }
    return startTurn(t);
}

bool WorkspaceChats::startTurn(ChatTurn *t)
{
    bool ok = false;
    const QString content = m_workspace->read(t->m_path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    // Whole turns from the oldest left out when the conversation does not fit the model.
    const int from = chat::firstSent(conversation, historyBudget());
    const QVariantList history = chat::history(conversation, from, seesPictures());
    if (!ok || history.isEmpty() || history.last().toMap().value(u"role"_s) != u"user"_s) {
        t->m_running = false;
        t->m_queued = false;
        Q_EMIT t->stateChanged();
        setFailure(t, tr("There is no question to answer: write one."));
        return false;
    }
    t->m_key = t->m_path;
    m_byKey.insert(t->m_key, t);
    t->m_running = true;
    t->m_queued = false;
    t->m_place = 0;
    t->m_omitted = from;
    t->m_status.clear();
    t->m_failure.clear();
    t->clearPartial();
    if (onDevice())
        m_onDevice = t;
    Q_EMIT t->stateChanged();
    Q_EMIT stateChanged();
    // (A missing partner fails at once, through failed().)
    m_extensions->runChat(t->m_key, history, conversation.page, conversation.partner, from);
    return true;
}

void WorkspaceChats::startWaiting()
{
    while (!m_waiting.isEmpty() && (!onDevice() || !m_onDevice)) {
        QPointer<ChatTurn> next = m_waiting.takeFirst();
        if (!next || !next->m_queued)
            continue;
        next->m_queued = false;
        startTurn(next);
    }
    updatePlaces();
}

void WorkspaceChats::updatePlaces()
{
    int place = 0;
    for (const QPointer<ChatTurn> &t : std::as_const(m_waiting)) {
        if (!t || !t->m_queued)
            continue;
        if (t->m_place != ++place) {
            t->m_place = place;
            Q_EMIT t->stateChanged();
        }
    }
}

void WorkspaceChats::answered(const QString &key, const QString &text)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return; // (a stopped turn's)
    const QString path = t->m_path;
    bool ok = false;
    const QString content = m_workspace->exists(path) ? m_workspace->read(path, &ok) : QString();
    if (!ok
        || !m_workspace->write(path, chat::appendMessage(content, chat::Role::Assistant, text, m_workspace->now(),
                                                         m_extensions->language()))) {
        qCWarning(logging::lcApp) << "AI chat: the answer could not be written to" << path;
        endTurn(t);
        setFailure(t, tr("The answer could not be saved."));
        return;
    }
    // Written before the turn ends: a view shows the message in place of the answer being
    // written, never neither of them.
    Q_EMIT written(path);
    endTurn(t);
}

void WorkspaceChats::failed(const QString &key, const QString &error)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return;
    endTurn(t);
    setFailure(t, error);
}

void WorkspaceChats::stop(const QString &path)
{
    ChatTurn *t = find(path);
    if (!t || !t->m_running)
        return;
    if (t->m_queued) {
        m_waiting.removeAll(QPointer<ChatTurn>(t));
        t->m_queued = false;
        endTurn(t);
        return;
    }
    const QString key = t->m_key;
    endTurn(t);
    m_extensions->stopChat(key);
}

void WorkspaceChats::stopAll()
{
    for (ChatTurn *t : m_turns.values()) {
        if (t->m_running)
            stop(t->m_path);
    }
}

void WorkspaceChats::dismissFailure(const QString &path) { setFailure(find(path), QString()); }

void WorkspaceChats::reset()
{
    m_waiting.clear();
    m_byKey.clear();
    m_onDevice = nullptr;
    for (ChatTurn *t : std::as_const(m_turns)) {
        t->m_key.clear();
        t->m_running = false;
        t->m_queued = false;
        t->m_place = 0;
        t->m_status.clear();
        t->m_failure.clear();
        t->m_omitted = 0;
        t->clearPartial();
        Q_EMIT t->stateChanged();
    }
    for (const QString &picture : std::exchange(m_added, {}))
        m_workspace->discardImport(picture);
    Q_EMIT stateChanged();
}

void WorkspaceChats::endTurn(ChatTurn *t)
{
    if (!t)
        return;
    if (!t->m_key.isEmpty() && m_byKey.value(t->m_key) == t)
        m_byKey.remove(t->m_key);
    t->m_key.clear();
    t->m_running = false;
    t->m_queued = false;
    t->m_place = 0;
    t->m_status.clear();
    t->clearPartial();
    Q_EMIT t->stateChanged();
    const bool freed = m_onDevice == t;
    if (freed)
        m_onDevice = nullptr;
    Q_EMIT stateChanged();
    if (freed || !onDevice())
        startWaiting();
    else
        updatePlaces();
}

void WorkspaceChats::setFailure(ChatTurn *t, const QString &failure)
{
    if (!t || t->m_failure == failure)
        return;
    t->m_failure = failure;
    Q_EMIT t->stateChanged();
    Q_EMIT stateChanged();
}

// ---- the list's menu ------------------------------------------------------------------------------

QString WorkspaceChats::nameOf(const QString &path) const { return chat::splitTitle(paths::pageTitle(path)).second; }

QString WorkspaceChats::rename(const QString &path, const QString &name)
{
    const QString words = paths::sanitizeFileName(name.simplified()).trimmed();
    if (words.isEmpty() || words == u"Untitled" || !m_workspace->exists(path))
        return {};
    const QString date = chat::splitTitle(paths::pageTitle(path)).first;
    const QString title = date.isEmpty() ? words : date + u' ' + words;
    if (title == paths::pageTitle(path))
        return path;
    return m_workspace->renamePage(path, title);
}

QString WorkspaceChats::remove(const QString &path)
{
    if (path.isEmpty() || !m_workspace->exists(path))
        return {};
    stop(path);
    return m_workspace->deletePage(path);
}

// ---- who answers ----------------------------------------------------------------------------------

QString WorkspaceChats::partnerOf(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    return ok ? chat::parse(content).partner : QString();
}

bool WorkspaceChats::setPartner(const QString &path, const QString &partner)
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return false;
    const QString changed = chat::setPartner(content, partner);
    return changed == content || m_workspace->write(path, changed);
}

// ---- pictures -------------------------------------------------------------------------------------

QString WorkspaceChats::addPicture(const QUrl &file)
{
    if (!m_workspace->ready() || !file.isValid())
        return {};
    const QString name = file.isLocalFile() ? file.toLocalFile() : file.toString();
    // (A content:// document of Android names no extension: its type is the picker's.)
    if (file.isLocalFile() && !paths::pictureExtensions().contains(paths::extension(name).toLower()))
        return {};
    const QString path = m_workspace->importAttachment(file);
    if (!path.isEmpty() && !m_added.contains(path))
        m_added.append(path);
    return path;
}

QStringList WorkspaceChats::addPictures(const QList<QUrl> &files)
{
    QStringList stored;
    for (const QUrl &file : files) {
        const QString path = addPicture(file);
        if (!path.isEmpty())
            stored.append(path);
    }
    if (stored.size() < files.size())
        Q_EMIT picturesRefused();
    return stored;
}

QStringList WorkspaceChats::addPicturesTo(const QStringList &attached, const QList<QUrl> &files)
{
    QStringList out = attached;
    for (const QString &path : addPictures(files)) {
        if (!out.contains(path))
            out.append(path);
    }
    return out;
}

QString WorkspaceChats::addClipboardPicture()
{
    const QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
    const QMimeData *data = clipboard ? clipboard->mimeData() : nullptr;
    if (!data || !m_workspace->ready())
        return {};
    // Files copied in a file manager: the first picture among them.
    if (data->hasUrls()) {
        for (const QUrl &url : data->urls()) {
            if (url.isLocalFile() && paths::pictureExtensions().contains(paths::extension(url.toLocalFile()).toLower()))
                return addPicture(url);
        }
    }
    // (Text with a picture — a browser's copy of a page — is pasted as text.)
    if (!data->hasImage() || (data->hasText() && !data->text().trimmed().isEmpty()))
        return {};
    const QImage image = qvariant_cast<QImage>(data->imageData());
    QByteArray png;
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    const QString name = tr("Pasted image %1").arg(m_workspace->now().toString(u"yyyyMMddHHmmss"_s)) + u".png"_s;
    const QString path = m_workspace->importAttachment(name, png);
    if (!path.isEmpty() && !m_added.contains(path))
        m_added.append(path);
    return path;
}

void WorkspaceChats::dropPicture(const QString &path)
{
    if (m_added.removeAll(path) > 0)
        m_workspace->discardImport(path);
}

QString WorkspaceChats::picturePath(const QString &name, const QString &conversation) const
{
    if (name.isEmpty() || !m_workspace->ready())
        return {};
    return m_workspace->resolveLink(name, conversation);
}

// ---- the camera and dictation ---------------------------------------------------------------------

bool WorkspaceChats::cameraAvailable() const { return platform::cameraAvailable(); }

bool WorkspaceChats::dictationAvailable() const { return platform::speechRecognizer() != nullptr; }

bool WorkspaceChats::listening() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech && speech->listening();
}

bool WorkspaceChats::takePhoto()
{
    const QPointer<WorkspaceChats> self(this);
    return platform::takePhoto([self](const QString &file, const QString &error) {
        if (!self)
            return;
        if (file.isEmpty()) {
            if (!error.isEmpty())
                Q_EMIT self->captureFailed(error);
            return;
        }
        const QString path = self->addPicture(QUrl::fromLocalFile(file));
        if (path.isEmpty())
            Q_EMIT self->captureFailed(tr("The photo could not be stored in the notebook."));
        else
            Q_EMIT self->photoTaken(path);
    });
}

void WorkspaceChats::connectSpeech()
{
    platform::SpeechRecognizer *speech = platform::speechRecognizer();
    if (!speech || m_speech == speech)
        return;
    if (m_speech)
        disconnect(m_speech, nullptr, this, nullptr);
    m_speech = speech;
    connect(speech, &platform::SpeechRecognizer::partial, this, &WorkspaceChats::dictationPartial);
    connect(speech, &platform::SpeechRecognizer::recognized, this, &WorkspaceChats::dictated);
    connect(speech, &platform::SpeechRecognizer::failed, this, &WorkspaceChats::dictationFailed);
    connect(speech, &platform::SpeechRecognizer::listeningChanged, this, &WorkspaceChats::dictationChanged);
}

bool WorkspaceChats::startDictation()
{
    connectSpeech();
    platform::SpeechRecognizer *speech = platform::speechRecognizer();
    if (!speech)
        return false;
    const bool started = speech->start(m_extensions->language() == u"en" ? u"en-GB"_s : u"pl-PL"_s);
    Q_EMIT dictationChanged();
    return started;
}

void WorkspaceChats::stopDictation()
{
    if (platform::SpeechRecognizer *speech = platform::speechRecognizer())
        speech->stop();
}

void WorkspaceChats::cancelDictation()
{
    if (platform::SpeechRecognizer *speech = platform::speechRecognizer())
        speech->cancel();
    Q_EMIT dictationChanged();
}

QUrl WorkspaceChats::pictureUrl(const QString &name, const QString &conversation) const
{
    if (name.isEmpty() || !m_workspace->ready())
        return {};
    const QString resolved = m_workspace->resolveLink(name, conversation);
    return m_workspace->assetUrl(resolved.isEmpty() ? name : resolved);
}

} // namespace brulion::shell
