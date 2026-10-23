// HistoryModel — the device-local versions of one page for HistoryView (vault/history.h through
// the Workspace), newest first. Each version is the text a change replaced; `reason` says what
// replaced it (your edit, another program, sync, an import, the command line — brulion-cli, e.g.
// an AI agent —, Brulion itself).
//
//   HistoryModel { workspace: Workspace; path: route.path }
//
// Roles: id, time ("YYYY-MM-DDTHH:mm:ss"), when ("dziś 09:12"), clock ("09:12"), day (section
// label: "Dziś", "Wczoraj", "pt 17 paź", "17 paź 2025"), size (bytes), sizeLabel ("1,2 kB"),
// reason ("user" | "external" | "sync" | "import" | "system" | "cli" | other), reasonLabel
// (translated), reasonIcon (Lucide name), newest (the first row).
// currentText / exists: the page as it is now (a deleted page has versions but no file).
#pragma once

#include "workspacemodel.h"

#include <QDateTime>

namespace brulion::content {

class HistoryModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// "pl" (default) | "en": labels of times and sizes.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// "YYYY-MM-DD" for "dziś"/"wczoraj"; "" = the workspace clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool exists READ exists NOTIFY currentChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TimeRole,
        WhenRole,
        ClockRole,
        DayRole,
        SizeRole,
        SizeLabelRole,
        ReasonRole,
        ReasonLabelRole,
        ReasonIconRole,
        NewestRole,
    };
    Q_ENUM(Role)

    explicit HistoryModel(QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString today() const { return m_today; }
    void setToday(const QString &today);
    QString currentText() const { return m_current; }
    bool exists() const { return m_exists; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    /// Text of the version in `row` ("" for a bad row).
    Q_INVOKABLE QString textAt(int row) const;
    /// Write the version in `row` back to the page (the current text is kept as a version first).
    Q_INVOKABLE bool restore(int row);
    /// Row of version `id`, -1 when it is not (or no longer) listed.
    Q_INVOKABLE int rowOf(const QString &id) const;

    /// Translated label of a reason ("user" -> "Replaced by your edit").
    static QString reasonLabel(const QString &reason);
    static QString reasonIcon(const QString &reason);

Q_SIGNALS:
    void pathChanged();
    void localeChanged();
    void todayChanged();
    void currentChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Version {
        QString id;
        QDateTime time;
        qint64 size = 0;
        QString reason;

        friend bool operator==(const Version &, const Version &) = default;
    };
    QDate effectiveToday() const;

    QList<Version> m_rows;
    QString m_path;
    QString m_locale = QStringLiteral("pl");
    QString m_today;
    QString m_current;
    bool m_exists = false;
};

} // namespace brulion::content
