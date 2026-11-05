// CleanupSelection — what CleanupView shows and what is ticked there (docs/UI.md "Cleaning up"),
// computed in C++ so the view's QML compiles: the rows of Workspace::unreferencedFiles()
// ({ path, title, folder, attachment, picture, pdf, size, sizeText, modified, openTasks,
// reminders }) of one kind, and the ticked paths among them.
//
//   CleanupSelection { files: …; kind: "notes"; ticked: root.ticked }
//
// The ticks are the view's (a list of paths it keeps and passes back in its route); the methods
// return a changed copy of them.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace brulion::shell {

class CleanupSelection : public QObject {
    Q_OBJECT
    /// Rows of Workspace::unreferencedFiles().
    Q_PROPERTY(QVariantList files READ files WRITE setFiles NOTIFY changed FINAL)
    /// "all" (default; also any unknown value) | "notes" | "attachments".
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY changed FINAL)
    /// The ticked paths (also paths not in `files`).
    Q_PROPERTY(QStringList ticked READ ticked WRITE setTicked NOTIFY changed FINAL)
    /// The rows of `kind`, in the order of `files`.
    Q_PROPERTY(QVariantList shown READ shown NOTIFY changed FINAL)
    /// The paths of the shown rows that are ticked, in the order of `files`.
    Q_PROPERTY(QStringList shownTicked READ shownTicked NOTIFY changed FINAL)
    /// Rows of `files` that are attachments (not notes).
    Q_PROPERTY(int attachmentCount READ attachmentCount NOTIFY changed FINAL)
    /// Shown and ticked rows with open tasks or reminders.
    Q_PROPERTY(int tickedWithWork READ tickedWithWork NOTIFY changed FINAL)

public:
    explicit CleanupSelection(QObject *parent = nullptr);

    QVariantList files() const { return m_files; }
    void setFiles(const QVariantList &files);
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    QStringList ticked() const { return m_ticked; }
    void setTicked(const QStringList &ticked);

    QVariantList shown() const { return m_shown; }
    QStringList shownTicked() const { return m_shownTicked; }
    int attachmentCount() const { return m_attachmentCount; }
    int tickedWithWork() const { return m_tickedWithWork; }

    /// `ticked` with `path` ticked (`on`) or not.
    Q_INVOKABLE QStringList withTicked(const QString &path, bool on) const;
    /// `ticked` with every shown row ticked (`on`) or none of them.
    Q_INVOKABLE QStringList withAllShown(bool on) const;
    /// `ticked` without the paths that are not in `files` any more.
    Q_INVOKABLE QStringList existingTicks() const;
    /// The ticks a route keeps for Back: its "selected" list of paths (none when missing).
    Q_INVOKABLE QStringList routeTicks(const QVariantMap &route) const;

Q_SIGNALS:
    void changed();

private:
    void update();

    QVariantList m_files;
    QString m_kind = QStringLiteral("all");
    QStringList m_ticked;
    QVariantList m_shown;
    QStringList m_shownTicked;
    int m_attachmentCount = 0;
    int m_tickedWithWork = 0;
};

} // namespace brulion::shell
