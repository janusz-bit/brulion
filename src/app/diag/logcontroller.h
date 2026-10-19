// LogController — the event log for QML (singleton `Logs`): copy, share, save, clear, the
// export header, the detailed mode. Wraps Logger::instance() (installed by main.cpp); without
// a logger (tests, tools) `available` is false and every action does nothing.
//
//   Logs.copy(model)        the shown entries (or everything) to the clipboard (≤ 256 KB)
//   Logs.share()            Android: the export as a file through the system's share sheet
//   Logs.saveToDownloads()  Android: the export into Downloads (MediaStore)
//   Logs.saveTo(url)        desktop / Android "Save as": the export to a picked file
//   Logs.download()         browser: the export as a download
//   Logs.openFolder()       desktop: the folder with brulion.log
//   Logs.clear()            empty the log (files too)
//   Logs.detailed           detailed mode: brulion.* debug lines, one line per HTTP request,
//                           3 MB files instead of 1 MB (persisted by main.cpp's hook)
//   Logs.actions            what this platform offers: "copy", "share", "downloads", "saveAs",
//                           "download", "openFolder"
// done(message) / failed(message) carry translated toast texts.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace brulion::diag {

class LogController : public QObject {
    Q_OBJECT
    /// A Logger is installed.
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// Warnings and errors recorded since start (a badge in the settings).
    Q_PROPERTY(int problemCount READ problemCount NOTIFY countsChanged FINAL)
    /// The files hold entries of earlier sessions.
    Q_PROPERTY(bool hasEarlier READ hasEarlier NOTIFY countsChanged FINAL)
    /// Desktop: the folder of the log files ("" elsewhere).
    Q_PROPERTY(QString location READ location CONSTANT FINAL)
    Q_PROPERTY(QStringList actions READ actions CONSTANT FINAL)
    Q_PROPERTY(bool detailed READ detailed WRITE setDetailed NOTIFY detailedChanged FINAL)
    Q_PROPERTY(QString suggestedFileName READ suggestedFileName NOTIFY countsChanged FINAL)

public:
    explicit LogController(QObject *parent = nullptr);
    ~LogController() override;

    /// The app's controller (main.cpp); nullptr before. The QML singleton returns it.
    static LogController *instance();

    /// Called with "normal" / "detailed" when the user switches (main.cpp stores it).
    void setPersistVerbosity(std::function<void(const QString &)> persist);
    /// Extra header lines: the recent sync runs (WorkspaceSync).
    void setRecentSyncs(std::function<QStringList()> provider);
    /// Clock for the header and the file name (default: now).
    void setClock(std::function<QDateTime()> clock);

    bool available() const;
    int problemCount() const;
    bool hasEarlier() const;
    QString location() const;
    QStringList actions() const;
    bool detailed() const;
    void setDetailed(bool on);
    QString suggestedFileName() const;

    /// Where "Save as" starts: the Downloads (else Documents, home) folder + suggestedFileName.
    Q_INVOKABLE QUrl suggestedSaveFile() const;
    /// The lines that start every export (translated).
    Q_INVOKABLE QStringList exportHeader() const;
    /// The whole export: header + the files (or the memory log), redacted.
    Q_INVOKABLE QString exportText() const;
    /// `model` (a LogModel): its shown entries; null: the whole export.
    Q_INVOKABLE bool copy(QObject *model = nullptr);
    /// Any text to the clipboard ("copy line", "copy details"); no toast.
    Q_INVOKABLE bool copyText(const QString &text);
    Q_INVOKABLE bool share();
    Q_INVOKABLE bool saveToDownloads();
    Q_INVOKABLE bool saveTo(const QUrl &url);
    Q_INVOKABLE void download();
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void countsChanged();
    void detailedChanged();
    void done(const QString &message);
    void failed(const QString &message);

private:
    void onAppended();
    QDateTime now() const;

    std::function<void(const QString &)> m_persist;
    std::function<QStringList()> m_recentSyncs;
    std::function<QDateTime()> m_clock;
    int m_problems = 0;
    bool m_hasEarlier = false;
};

} // namespace brulion::diag
