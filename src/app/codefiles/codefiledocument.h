// CodeFileDocument (QML `CodeFileDocument`) — the file viewer's model (docs/CODE-FILES.md "The
// file viewer"): one file of a project read off the GUI thread (core codefiles::readForViewer: at
// most 2 MiB, cut at a line; binary files and pictures told apart) and lexed for its colours
// (core codefiles::lexCodeLine), a row per line.
//
//   CodeFileDocument { folder: root.folder; path: "src/main.cpp"; palette: ({ keyword: Theme.link, … }) }
//   ListView { model: document; delegate: … required property int number; required property string html }
//
// Roles: number (1-based), html (the line as StyledText: escaped, tabs as spaces, each span in its
// palette colour; at most 2 000 characters of a line), text (the line as it is). `palette` maps
// codeTokenName()s to colours (a kind without one: the text's colour). `kind`: "" nothing,
// "loading" (another file being read: the same one read again keeps its rows until the new ones
// come), "text", "picture", "binary", "missing", "folder", "unreadable", "outside" (a path that
// leaves the folder: never read). A path is relative to `folder`, "/"-separated; one that
// leaves it is refused.
#pragma once

#include "codefiles/highlight.h"

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace brulion::codefiles {

class CodeFileDocument : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// {tokenName: colour} (QML colours or colour strings).
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY tabWidthChanged FINAL)
    Q_PROPERTY(QString kind READ kind NOTIFY loaded FINAL)
    Q_PROPERTY(bool truncated READ truncated NOTIFY loaded FINAL)
    Q_PROPERTY(qint64 size READ size NOTIFY loaded FINAL)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY loaded FINAL)
    /// The longest line's length in columns (tabs expanded; at most MaxLineLength): the view's width.
    Q_PROPERTY(int maxColumns READ maxColumns NOTIFY loaded FINAL)
    /// languageForPath()'s name ("" none).
    Q_PROPERTY(QString language READ language NOTIFY loaded FINAL)
    Q_PROPERTY(QString absolutePath READ absolutePath NOTIFY pathChanged FINAL)
    /// A picture's file: URL (kind "picture").
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY pathChanged FINAL)

public:
    enum Role { NumberRole = Qt::UserRole + 1, HtmlRole, TextRole };
    Q_ENUM(Role)

    /// The longest part of a line shown.
    static constexpr int MaxLineLength = 2000;

    explicit CodeFileDocument(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    int tabWidth() const { return m_tabWidth; }
    void setTabWidth(int width);
    QString kind() const { return m_kind; }
    bool truncated() const { return m_truncated; }
    qint64 size() const { return m_size; }
    int lineCount() const { return int(m_lines.size()); }
    int maxColumns() const { return m_maxColumns; }
    QString language() const { return m_language; }
    QString absolutePath() const;
    QUrl fileUrl() const;

    /// Read the file again (it changed).
    Q_INVOKABLE void reload();
    /// Lines `from` … `to` (1-based, either order, clamped) joined with "\n".
    Q_INVOKABLE QString linesText(int from, int to) const;
    /// One line's text (1-based; "" out of range).
    Q_INVOKABLE QString lineText(int line) const;

    /// The StyledText of `line` with `spans` in `colours` (pure; tabs to `tabWidth` spaces).
    static QString lineHtml(QStringView line, const QList<CodeSpan> &spans, const QHash<int, QString> &colours,
                            int tabWidth);

Q_SIGNALS:
    void folderChanged();
    void pathChanged();
    void paletteChanged();
    void tabWidthChanged();
    /// A read finished (or was refused): kind and the rows are new.
    void loaded();

private:
    struct Loaded;
    void load();
    void apply(Loaded result);

    QString m_folder;
    QString m_path;
    /// The absolute path the rows are of ("" none).
    QString m_loadedPath;
    QVariantMap m_palette;
    QHash<int, QString> m_colours;
    int m_tabWidth = 4;
    QString m_kind;
    bool m_truncated = false;
    qint64 m_size = 0;
    QString m_language;
    int m_maxColumns = 0;
    QStringList m_lines;
    QList<QList<CodeSpan>> m_spans;
    int m_generation = 0;
};

} // namespace brulion::codefiles
