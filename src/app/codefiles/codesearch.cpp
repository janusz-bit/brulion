#include "codesearch.h"

#include "codefileindex.h"
#include "codefilesbackground_p.h"

#include "codefiles/fileindex.h"
#include "codefiles/viewer.h"
#include "gitcli/client.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

namespace {

constexpr int TimeoutMs = 30000;
constexpr qsizetype MaxOutput = 32 * 1024 * 1024;

void appendEscaped(QString &out, QStringView text)
{
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'<': out += u"&lt;"_s; break;
        case u'>': out += u"&gt;"_s; break;
        case u'&': out += u"&amp;"_s; break;
        case u'"': out += u"&quot;"_s; break;
        case u'\t': out += u' '; break;
        default:
            if (c.unicode() >= 0x20)
                out += c;
        }
    }
}

QString cleanFolder(const QString &folder)
{
    return folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
}

} // namespace

QString CodeSearch::matchHtml(const SearchMatch &match, const QString &mark)
{
    // (Leading blanks are not shown: the ranges move with the text.)
    qsizetype skip = 0;
    while (skip < match.text.size() && (match.text.at(skip) == u' ' || match.text.at(skip) == u'\t'))
        ++skip;
    if (!match.ranges.isEmpty())
        skip = std::min<qsizetype>(skip, match.ranges.first().first);
    const QStringView text = QStringView(match.text).sliced(skip);
    QString out;
    out.reserve(text.size() + match.ranges.size() * 40);
    qsizetype at = 0;
    for (const auto &[from, to] : match.ranges) {
        const qsizetype a = std::clamp<qsizetype>(from - skip, at, text.size());
        const qsizetype b = std::clamp<qsizetype>(to - skip, a, text.size());
        appendEscaped(out, text.sliced(at, a - at));
        if (b > a) {
            out += u"<b><font color=\""_s + mark + u"\">"_s;
            appendEscaped(out, text.sliced(a, b - a));
            out += u"</font></b>"_s;
        }
        at = b;
    }
    appendEscaped(out, text.sliced(at));
    return out;
}

CodeSearch::CodeSearch(QObject *parent) : QAbstractListModel(parent), m_git(new gitcli::Client(this))
{
    m_git->setLocalTimeoutMs(TimeoutMs);
}

CodeSearch::~CodeSearch()
{
    cancel();
}

int CodeSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> CodeSearch::roleNames() const
{
    return {{KindRole, "kind"},   {PathRole, "path"}, {LineRole, "line"},    {HtmlRole, "html"},
            {CountRole, "matches"}, {NameRole, "name"}, {FolderRole, "folder"}};
}

QVariant CodeSearch::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= int(m_rows.size()))
        return {};
    const Row &r = m_rows.at(row);
    switch (role) {
    case KindRole: return r.file ? u"file"_s : u"match"_s;
    case PathRole: return r.path;
    case LineRole: return r.line;
    case HtmlRole: return r.html;
    case CountRole: return r.count;
    case NameRole: return r.path.mid(r.path.lastIndexOf(u'/') + 1);
    case FolderRole: {
        const qsizetype slash = r.path.lastIndexOf(u'/');
        return slash < 0 ? QString() : r.path.left(slash);
    }
    default: return {};
    }
}

void CodeSearch::setFolder(const QString &folder)
{
    const QString clean = cleanFolder(folder);
    if (clean == m_folder)
        return;
    m_folder = clean;
    Q_EMIT folderChanged();
    clear();
}

void CodeSearch::setCaseSensitive(bool on)
{
    if (on == m_options.caseSensitive)
        return;
    m_options.caseSensitive = on;
    Q_EMIT optionsChanged();
}

void CodeSearch::setWholeWord(bool on)
{
    if (on == m_options.wholeWord)
        return;
    m_options.wholeWord = on;
    Q_EMIT optionsChanged();
}

void CodeSearch::setRegex(bool on)
{
    if (on == m_options.regex)
        return;
    m_options.regex = on;
    Q_EMIT optionsChanged();
}

void CodeSearch::setMaxMatches(int n)
{
    n = std::clamp(n, 1, 100000);
    if (n == m_options.maxMatches)
        return;
    m_options.maxMatches = n;
    Q_EMIT optionsChanged();
}

void CodeSearch::setMarkColor(const QColor &color)
{
    if (color == m_markColor)
        return;
    m_markColor = color;
    Q_EMIT optionsChanged();
}

void CodeSearch::setGitProgram(const QString &program)
{
    m_git->setGitProgram(program);
}

QString CodeSearch::pathAt(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows.at(row).path : QString();
}

int CodeSearch::lineAt(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows.at(row).line : 0;
}

int CodeSearch::nextMatch(int row, int step) const
{
    step = step < 0 ? -1 : 1;
    for (int r = row + step; r >= 0 && r < int(m_rows.size()); r += step) {
        if (!m_rows.at(r).file)
            return r;
    }
    return -1;
}

void CodeSearch::reset()
{
    beginResetModel();
    m_rows.clear();
    m_files.clear();
    endResetModel();
    m_matchCount = 0;
    m_truncated = false;
    m_error.clear();
}

void CodeSearch::clear()
{
    cancel();
    reset();
    m_options.pattern.clear();
    m_done = false;
    m_engine.clear();
    Q_EMIT stateChanged();
}

void CodeSearch::cancel()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_cancel.reset();
    if (m_process) {
        QProcess *p = m_process;
        m_process = nullptr;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(1000);
        p->deleteLater();
    }
    m_buffer.clear();
    if (m_running) {
        m_running = false;
        Q_EMIT stateChanged();
    }
}

void CodeSearch::start(const QString &pattern)
{
    cancel();
    reset();
    m_options.pattern = pattern;
    m_done = false;
    m_engine.clear();
    if (pattern.isEmpty() || m_folder.isEmpty()) {
        Q_EMIT stateChanged();
        return;
    }
    if (const QString error = patternError(m_options); !error.isEmpty()) {
        finish(tr("This is no regular expression: %1").arg(error));
        return;
    }
    m_running = true;
    Q_EMIT stateChanged();
    if (!m_rgChosen) {
        m_rgChosen = true;
        if (m_rg.isEmpty())
            m_rg = QStandardPaths::findExecutable(u"rg"_s);
    }
    if (!m_rg.isEmpty() && gitcli::Client::processesAvailable())
        runRipgrep();
    else
        runGitGrep();
}

void CodeSearch::finish(const QString &error)
{
    m_running = false;
    m_done = true;
    m_error = error;
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    Q_EMIT stateChanged();
}

void CodeSearch::add(const QList<SearchMatch> &matches)
{
    if (matches.isEmpty())
        return;
    const QString mark = m_markColor.name(QColor::HexRgb);
    QList<Row> added;
    QHash<QString, int> counts;
    QString lastPath = m_rows.isEmpty() ? QString() : m_rows.constLast().path;
    for (const SearchMatch &m : matches) {
        if (m_matchCount >= m_options.maxMatches) {
            m_truncated = true;
            break;
        }
        if (m.path != lastPath || !m_files.contains(m.path)) {
            if (!m_files.contains(m.path)) {
                m_files.insert(m.path, int(m_rows.size() + added.size()));
                added.append({true, m.path, 0, QString(), 0});
            }
            lastPath = m.path;
        }
        added.append({false, m.path, m.line, matchHtml(m, mark), 0});
        ++counts[m.path];
        ++m_matchCount;
    }
    if (!added.isEmpty()) {
        beginInsertRows({}, int(m_rows.size()), int(m_rows.size() + added.size()) - 1);
        m_rows.append(added);
        endInsertRows();
    }
    for (const auto [path, count] : counts.asKeyValueRange()) {
        const int row = m_files.value(path, -1);
        if (row < 0)
            continue;
        m_rows[row].count += count;
        Q_EMIT dataChanged(index(row), index(row), {CountRole});
    }
    Q_EMIT stateChanged();
}

void CodeSearch::runRipgrep()
{
    m_engine = u"rg"_s;
    auto *p = new QProcess(this);
    m_process = p;
    p->setProgram(m_rg);
    p->setArguments(ripgrepArguments(m_options));
    p->setWorkingDirectory(m_folder);
    p->setStandardInputFile(QProcess::nullDevice());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(u"RIPGREP_CONFIG_PATH"_s);
    p->setProcessEnvironment(env);
    const int generation = m_generation;
    connect(p, &QProcess::readyReadStandardOutput, this, [this, generation] {
        if (generation == m_generation)
            readRipgrep();
    });
    connect(p, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError e) {
        if (generation != m_generation || e != QProcess::FailedToStart)
            return;
        // (rg went away: git grep then.)
        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
        m_rg.clear();
        runGitGrep();
    });
    connect(p, &QProcess::finished, this, [this, generation, p](int code, QProcess::ExitStatus status) {
        if (generation != m_generation)
            return;
        readRipgrep();
        if (generation != m_generation)
            return;
        // Exit code 1: nothing found; 2: an error (some files unreadable — the matches stand).
        QString error;
        if (status != QProcess::NormalExit && !m_truncated)
            error = tr("The search stopped.");
        else if (code >= 2 && m_matchCount == 0)
            error = QString::fromUtf8(p->readAllStandardError()).trimmed().section(u'\n', 0, 0);
        finish(error);
    });
    QTimer::singleShot(TimeoutMs, p, [this, generation, p] {
        if (generation != m_generation || p->state() == QProcess::NotRunning)
            return;
        m_truncated = true;
        p->kill();
    });
    p->start();
}

void CodeSearch::readRipgrep()
{
    if (!m_process)
        return;
    m_buffer += m_process->readAllStandardOutput();
    QList<SearchMatch> matches;
    qsizetype from = 0;
    for (qsizetype nl = m_buffer.indexOf('\n'); nl >= 0; nl = m_buffer.indexOf('\n', from)) {
        if (std::optional<SearchMatch> m = parseRipgrepLine(QByteArrayView(m_buffer).sliced(from, nl - from)))
            matches.append(std::move(*m));
        from = nl + 1;
    }
    m_buffer.remove(0, from);
    if (m_buffer.size() > MaxOutput)
        m_buffer.clear();
    add(matches);
    if (m_truncated && m_process && m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void CodeSearch::runGitGrep()
{
    m_engine = u"git"_s;
    if (m_git->gitProgram().isEmpty() || !gitcli::Client::processesAvailable()) {
        runOwn();
        return;
    }
    gitcli::RunOptions options;
    options.readOnly = true;
    options.maxOutput = MaxOutput;
    options.timeoutMs = TimeoutMs;
    const int generation = m_generation;
    const SearchOptions search = m_options;
    m_git->git(m_folder, gitGrepArguments(search), options, [this, generation, search](const gitcli::RunResult &r) {
        if (generation != m_generation)
            return;
        // Exit code 1 with nothing on standard error: nothing found.
        if (r.failure.isEmpty() && r.exitCode == 1 && r.err.trimmed().isEmpty()) {
            finish(QString());
            return;
        }
        if (r.ok() || r.outputCut) {
            add(parseGitGrep(r.out, search, search.maxMatches + 1));
            if (r.outputCut)
                m_truncated = true;
            finish(QString());
            return;
        }
        // No work tree (or git missing): Brulion's own search.
        runOwn();
    });
}

void CodeSearch::runOwn()
{
    m_engine = u"brulion"_s;
    Q_EMIT stateChanged();
    FileIndexCache *cache = FileIndexCache::instance();
    const std::shared_ptr<const FileIndex> index = cache->index(m_folder);
    const int generation = m_generation;
    if (!index) {
        // (Its index is being built: the search starts once it is there.)
        auto *wait = new QObject(this);
        connect(cache, &FileIndexCache::updated, wait, [this, wait, generation](const QString &folder) {
            if (folder != m_folder)
                return;
            if (generation != m_generation || FileIndexCache::instance()->index(m_folder)) {
                wait->deleteLater();
                if (generation == m_generation)
                    runOwn();
            }
        });
        return;
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const std::shared_ptr<std::atomic<bool>> cancel = m_cancel;
    const SearchOptions search = m_options;
    const QString root = m_folder;
    detail::onPool<QList<SearchMatch>>(
            this,
            [index, cancel, search, root] {
                QList<SearchMatch> out;
                const QRegularExpression expression = searchExpression(search);
                const QStringList files = index->files();
                for (const QString &path : files) {
                    if (cancel->load() || out.size() > search.maxMatches)
                        break;
                    QFile f(root + u'/' + path);
                    if (f.size() > search.maxFileSize || !f.open(QIODevice::ReadOnly))
                        continue;
                    const QByteArray bytes = f.readAll();
                    if (looksBinary(bytes))
                        continue;
                    out.append(searchText(path, QString::fromUtf8(bytes), expression,
                                          std::min(search.maxPerFile, int(search.maxMatches + 1 - out.size()))));
                }
                return out;
            },
            [this, generation](QList<SearchMatch> matches) {
                if (generation != m_generation)
                    return;
                add(matches);
                finish(QString());
            });
}

} // namespace brulion::codefiles
