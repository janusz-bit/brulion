// DrawingBackdrops — QML's way to backdrops (docs/UI.md "Drawing editor": "Rysunek na obrazie lub
// PDF…" in a note, "Podkład…" in the drawing editor): whether PDF pages can be drawn here, a PDF's
// pages, their thumbnails (PdfPageImageProvider) and backdrops prepared from vault files in the
// background (backdropimage.h).
//
//   const info = DrawingBackdrops.pdfInfo(path)          // { pages, error ("" | message), locked,
//                                                        //   width, height (page 1, points) }
//   const job = DrawingBackdrops.prepare(path, [1, 3])   // [] — the file is a picture; a third
//                                                        // `true`: the drawings' files made too
//   onProgress: (job, done, total) => …;  onFinished: (job, items) => controller.insertDrawingsOn(…)
//
// QML passes vault paths; the path resolver (setPathResolver(), main.cpp: Workspace's
// absoluteFilePath()) makes them files on the GUI thread before a job starts. A job prepares its
// files one after another on Qt's thread pool (the browser, which has no threads: on the GUI
// thread, one file per event-loop turn) and can be cancelled between them. Registered as the QML
// singleton "DrawingBackdrops" (drawingqml.h).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::drawingview {

class DrawingBackdrops : public QObject {
    Q_OBJECT
    /// PDF pages can be drawn on this device (platform/pdfpages.h; not in the browser).
    Q_PROPERTY(bool pdfAvailable READ pdfAvailable CONSTANT FINAL)
    /// The file picker's filters for a backdrop: pictures and — where they can be drawn — PDFs.
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT FINAL)

public:
    explicit DrawingBackdrops(QObject *parent = nullptr);
    ~DrawingBackdrops() override;

    /// A vault path → the file's absolute path ("" none). Process-wide (main.cpp; tests).
    static void setPathResolver(std::function<QString(const QString &)> resolver);
    static QString resolve(const QString &vaultPath);
    /// A file picked in the browser (its name and bytes) → its vault path in the notebook
    /// (Workspace::importAttachment; "" when it could not be stored). Process-wide (main.cpp).
    static void setImporter(std::function<QString(const QString &, const QByteArray &)> importer);

    bool pdfAvailable() const;
    QStringList nameFilters() const;

    /// The file at `vaultPath` is a PDF (by its name).
    Q_INVOKABLE bool isPdf(const QString &vaultPath) const;
    /// A PDF's pages: { pages (−1: not readable), error (a sentence for the user, "" none), locked,
    /// width, height (the first page's size in points; 0 unknown: the page picker's thumbnails take
    /// its shape) }.
    Q_INVOKABLE QVariantMap pdfInfo(const QString &vaultPath) const;
    /// The thumbnail of a PDF page: image://pdfpage/<file>?page=N&v=<modified>; empty when the file
    /// is not there or PDFs cannot be drawn.
    Q_INVOKABLE QUrl pageUrl(const QString &vaultPath, int page) const;
    /// Start preparing backdrops of the file `vaultPath`: its PDF pages `pages` (1-based), or —
    /// none given — the file as a picture. `drawings`: each item also becomes a new drawing's file
    /// (`svg`: core drawing::newDrawingOnBackdrop() written, its raster no longer in `bytes`) on
    /// the worker thread — PageController::insertDrawingsOn() then only stores it, so 60 slides
    /// do not freeze the window. The job's id (> 0); progress() and finished() follow (also for a
    /// file that cannot be read: its items carry the error).
    Q_INVOKABLE int prepare(const QString &vaultPath, const QList<int> &pages, bool drawings = false);
    /// Stop the job after the file being prepared: finished() comes with what was done, `cancelled`.
    Q_INVOKABLE void cancel(int job);
    /// The sentence for a prepared item's error code (backdropimage.h backdroperror).
    Q_INVOKABLE QString errorText(const QString &code) const;
    /// The pages a user typed ("1-3, 7", "wszystkie"; drawing::parsePageRanges — one more than
    /// MaxPagesPerInsert at most, so the dialog can say the text named too many) and back
    /// (drawing::formatPageRanges).
    Q_INVOKABLE QList<int> parsePages(const QString &text, int count) const;
    Q_INVOKABLE QString formatPages(const QList<int> &pages) const;
    /// The browser (no file URLs there): its own file picker for a picture, stored in the notebook
    /// through the importer — picked(pick, path) follows, pickFailed(pick, name) when it could not
    /// be read or stored; nothing when cancelled. The pick's id (> 0); 0 elsewhere (QML uses
    /// FilePicker and Workspace.importAttachment()).
    Q_INVOKABLE int pickInBrowser();

Q_SIGNALS:
    void progress(int job, int done, int total);
    /// `items`: { bytes (or svg), mimeType, width, height, path, page, name, error } in the order
    /// asked.
    void finished(int job, const QVariantList &items, bool cancelled);
    void picked(int pick, const QString &path);
    void pickFailed(int pick, const QString &name);

private:
    struct Job;
    void step(const std::shared_ptr<Job> &job);
    void done(const std::shared_ptr<Job> &job, bool cancelled);

    QHash<int, std::shared_ptr<Job>> m_jobs;
    int m_lastJob = 0;
    [[maybe_unused]] int m_lastPick = 0; // (the browser's picks)
};

} // namespace brulion::drawingview
