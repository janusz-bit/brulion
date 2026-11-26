// CodeSearch (QML `CodeSearch`) — the project search's model (docs/CODE-FILES.md "Project
// search"): the pattern searched for in a folder's files, results grouped by file.
//
//   CodeSearch { id: search; folder: root.folder; markColor: Theme.mark }
//   search.start("żółw")   …   rows: a "file" row (path, matches) before its "match" rows (line, html)
//
// Engines, the first there is: ripgrep (`rg --json …`, its matches as they come), `git grep` in a
// work tree (through gitcli::Client: the environment rules of docs/CODE-GIT.md "Security"),
// Brulion's own search of the folder's index (FileIndexCache: files up to 4 MiB, not binary ones)
// on a pool thread. Programs run with argument lists, no shell, standard input closed, at most 30 s
// and 32 MiB of output. At most `maxMatches` matches (2 000; `truncated`), 200 a file. start()
// again or cancel() stops what runs. The pattern is literal unless `regex`; `caseSensitive`,
// `wholeWord` as their names say (an invalid regular expression: `error`, nothing runs).
#pragma once

#include "codefiles/search.h"

#include <QAbstractListModel>
#include <QColor>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>

class QProcess;

namespace brulion::gitcli {
class Client;
}

namespace brulion::codefiles {

class CodeSearch : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    Q_PROPERTY(bool caseSensitive READ caseSensitive WRITE setCaseSensitive NOTIFY optionsChanged FINAL)
    Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY optionsChanged FINAL)
    Q_PROPERTY(bool regex READ regex WRITE setRegex NOTIFY optionsChanged FINAL)
    Q_PROPERTY(int maxMatches READ maxMatches WRITE setMaxMatches NOTIFY optionsChanged FINAL)
    /// The colour behind the matched text in the rows' html.
    Q_PROPERTY(QColor markColor READ markColor WRITE setMarkColor NOTIFY optionsChanged FINAL)
    /// The pattern of the last start() ("" none).
    Q_PROPERTY(QString pattern READ pattern NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged FINAL)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged FINAL)
    /// Why nothing was found ("" none): an invalid pattern, a program that failed.
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    /// "rg", "git", "brulion" ("" before a search).
    Q_PROPERTY(QString engine READ engine NOTIFY stateChanged FINAL)
    /// The search finished at least once (nothing found is a result).
    Q_PROPERTY(bool done READ done NOTIFY stateChanged FINAL)

public:
    enum Role { KindRole = Qt::UserRole + 1, PathRole, LineRole, HtmlRole, CountRole, NameRole, FolderRole };
    Q_ENUM(Role)

    explicit CodeSearch(QObject *parent = nullptr);
    ~CodeSearch() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    bool caseSensitive() const { return m_options.caseSensitive; }
    void setCaseSensitive(bool on);
    bool wholeWord() const { return m_options.wholeWord; }
    void setWholeWord(bool on);
    bool regex() const { return m_options.regex; }
    void setRegex(bool on);
    int maxMatches() const { return m_options.maxMatches; }
    void setMaxMatches(int n);
    QColor markColor() const { return m_markColor; }
    void setMarkColor(const QColor &color);
    QString pattern() const { return m_options.pattern; }
    bool running() const { return m_running; }
    int matchCount() const { return m_matchCount; }
    int fileCount() const { return int(m_files.size()); }
    bool truncated() const { return m_truncated; }
    QString error() const { return m_error; }
    QString engine() const { return m_engine; }
    bool done() const { return m_done; }

    /// Search for `pattern` (what runs stops; "" clears).
    Q_INVOKABLE void start(const QString &pattern);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    /// The row's path / line (the view's Enter); "" / 0 out of range.
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int lineAt(int row) const;
    /// The next row that is a match after `row` (`step` 1) or before it (-1); -1 none.
    Q_INVOKABLE int nextMatch(int row, int step) const;

    /// Tests: the programs ("" none: that engine is skipped).
    void setRipgrepProgram(const QString &program) { m_rg = program; }
    void setGitProgram(const QString &program);

    /// The StyledText of a match's text with its ranges behind `mark` (pure).
    static QString matchHtml(const SearchMatch &match, const QString &mark);

Q_SIGNALS:
    void folderChanged();
    void optionsChanged();
    void stateChanged();

private:
    struct Row {
        bool file = false;
        QString path;
        int line = 0;
        QString html;
        int count = 0;
    };
    void reset();
    void finish(const QString &error);
    void add(const QList<SearchMatch> &matches);
    void runRipgrep();
    void runGitGrep();
    void runOwn();
    void readRipgrep();

    QString m_folder;
    SearchOptions m_options;
    QColor m_markColor = QColor(0xff, 0xd8, 0x4d, 0x99);
    QList<Row> m_rows;
    /// path → its file row.
    QHash<QString, int> m_files;
    int m_matchCount = 0;
    bool m_running = false;
    bool m_truncated = false;
    bool m_done = false;
    QString m_error;
    QString m_engine;
    int m_generation = 0;
    QString m_rg;
    bool m_rgChosen = false;
    QPointer<QProcess> m_process;
    QByteArray m_buffer;
    gitcli::Client *m_git = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

} // namespace brulion::codefiles
