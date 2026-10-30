#include "importcontroller.h"

#include "log/categories.h"

#include "iobackground.h"

#include "io/importer.h"
#include "markdown/frontmatter.h"
#include "markdown/journal.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QEvent>
#include <QLocale>
#include <QRegularExpression>

#include "pickedfiles.h"
#ifdef Q_OS_WASM
#include "wasm/wasmfiles.h"
#endif

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

QString decodeUtf8(const QByteArray &bytes)
{
    QByteArrayView view = bytes;
    if (view.startsWith("\xEF\xBB\xBF"))
        view = view.sliced(3);
    return QString::fromUtf8(view);
}

QString normalizedNewlines(QString text)
{
    text.replace(u"\r\n"_s, u"\n"_s);
    text.replace(u'\r', u'\n');
    return text;
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

/// Every file and folder path of the vault, case-folded (ImportOptions::exists compares
/// case-insensitively). Taken on the GUI thread; the importer reads the copy on its thread.
QSet<QString> vaultPathSet(Vault *vault)
{
    QSet<QString> set;
    if (!vault)
        return set;
    for (const FileStat &f : vault->files())
        set.insert(f.path.toCaseFolded());
    for (const FileStat &f : vault->folders())
        set.insert(f.path.toCaseFolded());
    return set;
}

struct Analysis {
    QString detected;
    QString used;
    QString usedName;
    QString target;
    bool ran = false;
    io::ImportResult result;
};

Analysis analyzeImport(const QList<io::InputFile> &input, const QString &chosen, const QString &editedFolder,
                       bool folderEdited, io::ImportOptions options)
{
    Analysis a;
    if (const io::Importer *detected = io::detectImporter(input))
        a.detected = detected->id();
    const io::Importer *importer = io::importerById(chosen.isEmpty() ? a.detected : chosen);
    if (!importer)
        return a;
    a.used = importer->id();
    a.usedName = importer->name();
    a.target = folderEdited ? editedFolder : defaultImportFolder(importer->name());
    options.targetFolder = a.target;
    a.result = io::runImport(*importer, input, options);
    a.ran = true;
    return a;
}

} // namespace

// ---- Pure helpers ---------------------------------------------------------------------------------

QByteArray appendImportedJournal(const QByteArray &existing, const QByteArray &imported, const QString &source)
{
    QString addition = decodeUtf8(imported);
    if (const auto fm = markdown::findFrontmatter(addition))
        addition = addition.sliced(fm->bodyStart);
    addition = normalizedNewlines(addition);
    while (addition.startsWith(u'\n'))
        addition.remove(0, 1);
    while (!addition.isEmpty() && addition.back().isSpace())
        addition.chop(1);
    if (addition.isEmpty())
        return existing;
    const QString current = normalizedNewlines(decodeUtf8(existing));
    if (current.trimmed().isEmpty())
        return imported;
    // The same import again (the user imported the same export twice): nothing new to add.
    if (current.contains(addition))
        return existing;
    const QString heading = u"## Import (%1)"_s.arg(source.trimmed().isEmpty() ? u"?"_s : source.trimmed());
    return io::appendImported(existing, (heading + u"\n\n"_s + addition + u'\n').toUtf8());
}

QString importedKind(const VaultPath &path, bool append, const VaultConfig &config)
{
    if (append || markdown::journalDateFromPath(path, config))
        return u"journal"_s;
    return paths::isMarkdown(path) ? u"page"_s : u"attachment"_s;
}

QString defaultImportFolder(const QString &importerName)
{
    return u"Import/"_s + paths::sanitizeFileName(importerName);
}

std::optional<QString> cleanTargetFolder(const QString &folder)
{
    QString unified = folder.trimmed().normalized(QString::NormalizationForm_C);
    unified.replace(u'\\', u'/');
    QStringList segments;
    for (const QString &raw : unified.split(u'/')) {
        const QString seg = raw.trimmed();
        if (seg.isEmpty() || seg == u".")
            continue;
        if (seg == u"..")
            return std::nullopt;
        if (paths::sanitizeFileName(seg) != seg)
            return std::nullopt;
        segments.append(seg);
    }
    const QString clean = segments.join(u'/');
    if (!clean.isEmpty() && paths::isIgnored(clean + u"/x.md"_s))
        return std::nullopt;
    return clean;
}

// ---- ImportController -----------------------------------------------------------------------------

ImportController::ImportController(QObject *parent)
    : QObject(parent)
{
    m_analyzeTimer.setSingleShot(true);
    m_analyzeTimer.setInterval(350);
    connect(&m_analyzeTimer, &QTimer::timeout, this, &ImportController::analyze);
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(0);
    connect(&m_writeTimer, &QTimer::timeout, this, &ImportController::writeStep);

    connect(&m_gatherer, &FileGatherer::progress, this, [this](int done, int total) {
        setProgress(total > 0 ? qreal(done) / qreal(total) : -1,
                    total > 0 ? tr("Reading %1 of %2 files…").arg(done).arg(total) : tr("Looking for the files…"));
    });
    connect(&m_gatherer, &FileGatherer::finished, this,
            [this](const QList<io::InputFile> &files, const QStringList &warnings, const QString &name, qint64) {
                setInput(files, name, warnings);
            });
    // Switching the UI language installs another translator: the translated lists follow.
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

ImportController::~ImportController() = default;

bool ImportController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && watched == QCoreApplication::instance()) {
        Q_EMIT importersChanged();
        Q_EMIT importerChanged(); // effectiveName, nameFilters ("All files (*)")
    }
    return QObject::eventFilter(watched, event);
}

void ImportController::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace)
        connect(m_workspace, &shell::Workspace::openChanged, this, &ImportController::clear);
    Q_EMIT workspaceChanged();
}

QVariantList ImportController::importers() const
{
    QVariantList out;
    for (const io::Importer *importer : io::importers()) {
        const ImporterInfo info = importerInfo(importer->id());
        out.append(QVariantMap{
                {u"id"_s, importer->id()},
                {u"name"_s, importer->name()},
                {u"howTo"_s, info.howTo},
                {u"phoneHowTo"_s, info.phoneHowTo},
                {u"pick"_s, info.pick},
                {u"icon"_s, info.icon},
                {u"fileTypes"_s, importer->fileTypes()},
        });
    }
    return out;
}

bool ImportController::busy() const
{
    return m_state == u"reading" || m_state == u"analyzing" || m_state == u"importing";
}

const io::Importer *ImportController::effectiveImporter() const
{
    return io::importerById(m_importerId.isEmpty() ? m_detectedId : m_importerId);
}

QString ImportController::effectiveId() const
{
    const io::Importer *importer = effectiveImporter();
    return importer ? importer->id() : QString();
}

QString ImportController::effectiveName() const
{
    const io::Importer *importer = effectiveImporter();
    return importer ? importer->name() : QString();
}

QStringList ImportController::nameFilters() const
{
    QStringList filters;
    if (const io::Importer *importer = io::importerById(m_importerId)) {
        filters.append(u"%1 (%2)"_s.arg(importer->name(), importer->fileTypes().join(u' ')));
    } else {
        QStringList all;
        for (const io::Importer *i : io::importers()) {
            for (const QString &type : i->fileTypes()) {
                if (!all.contains(type))
                    all.append(type);
            }
        }
        filters.append(tr("Exports of notes apps (%1)").arg(all.join(u' ')));
    }
    filters.append(tr("All files (*)"));
    return filters;
}

QString ImportController::inputSizeText() const
{
    return sizeText(m_inputBytes);
}

void ImportController::setImporterId(const QString &id)
{
    const QString clean = io::importerById(id) ? id : QString();
    if (clean == m_importerId)
        return;
    if (m_state == u"importing")
        return;
    m_importerId = clean;
    updateDefaultFolder();
    Q_EMIT importerChanged();
    if (!m_input.isEmpty() && m_state != u"reading")
        analyze();
}

void ImportController::updateDefaultFolder()
{
    if (m_targetFolderEdited)
        return;
    const io::Importer *importer = effectiveImporter();
    const QString folder = importer ? defaultImportFolder(importer->name()) : QString();
    if (folder == m_targetFolder)
        return;
    m_targetFolder = folder;
    m_targetFolderError.clear();
    Q_EMIT targetFolderChanged();
}

void ImportController::setTargetFolder(const QString &folder)
{
    if (folder == m_targetFolder && m_targetFolderEdited)
        return;
    m_targetFolder = folder;
    m_targetFolderEdited = true;
    m_targetFolderError = cleanTargetFolder(folder)
                                  ? QString()
                                  : tr("Use a folder inside the notebook: no “..”, no protected folders (.trash, "
                                       ".brulion) and no characters such as : * ? \" < > |.");
    Q_EMIT targetFolderChanged();
    if (!m_input.isEmpty() && (m_state == u"ready" || m_state == u"analyzing" || m_state == u"error"))
        scheduleAnalyze();
}

void ImportController::setState(const QString &state, const QString &error)
{
    if (state == m_state && error == m_errorText)
        return;
    m_state = state;
    m_errorText = error;
    Q_EMIT stateChanged();
}

void ImportController::setProgress(qreal value, const QString &text)
{
    if (qFuzzyCompare(value + 2, m_progress + 2) && text == m_progressText)
        return;
    m_progress = value;
    m_progressText = text;
    Q_EMIT progressChanged();
}

// ---- Input ---------------------------------------------------------------------------------------

void ImportController::addUrls(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || m_state == u"importing")
        return;
    ++m_generation;
    m_analyzeTimer.stop();
    m_input.clear();
    m_detectedId.clear();
    m_preview.clear();
    m_report.clear();
    Q_EMIT previewChanged();
    Q_EMIT reportChanged();
    setState(u"reading"_s);
    setProgress(0, tr("Reading…"));
    m_gatherer.start(urls);
}

void ImportController::addFolder(const QUrl &url)
{
    if (url.isValid() && !url.isEmpty())
        addUrls({ url });
}

void ImportController::addPaths(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &p : paths) {
        if (const QUrl url = urlFromUserInput(p); !url.isEmpty())
            urls.append(url);
    }
    addUrls(urls);
}

void ImportController::pickInBrowser()
{
#ifdef Q_OS_WASM
    // Several files at once, as the desktop and Android pickers; the gatherer's limits apply
    // before a file is read.
    const QPointer<ImportController> self(this);
    const GatherLimits &limits = m_gatherer.limits();
    platform::wasm::openFiles(nameFilters(), true, limits.maxFileBytes, limits.maxTotalBytes,
                              [self](const QList<platform::PickedFile> &picked) {
                                  if (self)
                                      self->setPickedFiles(picked);
                              });
#endif
}

void ImportController::setPickedFiles(const QList<platform::PickedFile> &picked)
{
    if (picked.isEmpty())
        return; // cancelled
    QList<io::InputFile> files;
    QStringList warnings;
    bool overTotal = false;
    for (const platform::PickedFile &f : picked) {
        if (f.tooLarge)
            warnings.append(FileGatherer::tooLargeWarning(f.name, f.size));
        else if (f.overTotal)
            overTotal = true; // this one and every one after it (PickCollector)
        else
            files.append(io::InputFile{f.name, f.data});
    }
    if (overTotal)
        warnings.append(FileGatherer::totalLimitWarning(m_gatherer.limits().maxTotalBytes));
    setInput(files, FileGatherer::sourceNameFor(int(picked.size()), picked.first().name), warnings);
}

void ImportController::setInput(const QList<io::InputFile> &files, const QString &sourceName, const QStringList &warnings)
{
    if (m_state == u"importing")
        return; // a browser pick finishing during an import: the import goes on
    ++m_generation;
    m_input = files;
    m_sourceName = sourceName;
    m_readWarnings = warnings;
    m_inputBytes = 0;
    for (const io::InputFile &f : files)
        m_inputBytes += f.data.size();
    m_detectedId.clear();
    m_report.clear();
    Q_EMIT inputChanged();
    Q_EMIT importerChanged();
    Q_EMIT reportChanged();
    analyze();
}

void ImportController::clear()
{
    m_gatherer.cancel();
    m_analyzeTimer.stop();
    m_writeTimer.stop();
    ++m_generation;
    m_input.clear();
    m_inputBytes = 0;
    m_sourceName.clear();
    m_readWarnings.clear();
    m_detectedId.clear();
    m_preview.clear();
    m_report.clear();
    m_writeQueue.clear();
    m_targetFolderEdited = false;
    m_targetFolderError.clear();
    updateDefaultFolder();
    setProgress(0);
    Q_EMIT inputChanged();
    Q_EMIT importerChanged();
    Q_EMIT previewChanged();
    Q_EMIT reportChanged();
    Q_EMIT targetFolderChanged();
    setState(u"empty"_s);
}

void ImportController::cancel()
{
    if (m_state == u"reading") {
        clear();
    } else if (m_state == u"analyzing") {
        clear();
    } else if (m_state == u"importing") {
        m_cancelRequested = true;
    }
}

// ---- Dry run ---------------------------------------------------------------------------------------

void ImportController::scheduleAnalyze()
{
    m_analyzeTimer.start();
}

std::function<bool(const VaultPath &)> ImportController::existsSnapshot() const
{
    const QSet<QString> set = vaultPathSet(m_workspace ? m_workspace->vault() : nullptr);
    return [set](const VaultPath &path) { return set.contains(paths::normalize(path).toCaseFolded()); };
}

void ImportController::analyze()
{
    m_analyzeTimer.stop();
    if (!m_workspace || !m_workspace->vault()) {
        setState(u"error"_s, tr("Open a notebook first."));
        return;
    }
    if (m_input.isEmpty()) {
        setState(u"error"_s, m_readWarnings.isEmpty() ? tr("No files were read.") : m_readWarnings.first());
        return;
    }
    const std::optional<QString> folder = cleanTargetFolder(m_targetFolder);
    if (m_targetFolderEdited && !folder) {
        // The field shows why; nothing is analysed with an unusable folder.
        setState(u"ready"_s);
        return;
    }
    const int generation = ++m_generation;
    setState(u"analyzing"_s);
    setProgress(-1, tr("Checking what will be imported…"));

    io::ImportOptions options;
    options.config = m_workspace->vault()->config();
    options.exists = existsSnapshot();
    const QList<io::InputFile> input = m_input;
    const QString chosen = m_importerId;
    const QString edited = folder.value_or(QString());
    const bool folderEdited = m_targetFolderEdited;
    runInBackground<Analysis>(
            this, [input, chosen, edited, folderEdited, options] {
                return analyzeImport(input, chosen, edited, folderEdited, options);
            },
            [this, generation](Analysis a) {
                if (generation != m_generation)
                    return;
                m_detectedId = a.detected;
                if (!m_targetFolderEdited && a.target != m_targetFolder && !a.target.isEmpty()) {
                    m_targetFolder = a.target;
                    Q_EMIT targetFolderChanged();
                }
                Q_EMIT importerChanged();
                if (!a.ran) {
                    m_preview.clear();
                    Q_EMIT previewChanged();
                    setState(u"error"_s, tr("Brulion does not recognise these files. Choose the app they come "
                                            "from in the list."));
                    return;
                }
                applyPlan(generation, a.result, false);
            });
}

void ImportController::applyPlan(int generation, const io::ImportResult &result, bool forImport)
{
    if (generation != m_generation || !m_workspace || !m_workspace->vault())
        return;
    Vault *vault = m_workspace->vault();
    const VaultConfig config = vault->config();
    // "Plan 2.md" next to an existing "Plan.md": renamed because the name is taken.
    static const QRegularExpression numbered(u"^(.*) \\d+(\\.[^./]*)?$"_s);
    int pages = 0, journals = 0, appended = 0, attachments = 0, renamed = 0;
    qint64 bytes = 0;
    QVariantList files;
    for (const io::ImportedFile &f : result.files) {
        const QString kind = importedKind(f.path, f.append, config);
        const bool appends = f.append && vault->exists(f.path);
        if (!f.append) {
            const QRegularExpressionMatch m = numbered.match(f.path);
            // Taken also in another letter case ("dom 2.md" next to "Dom.md").
            if (m.hasMatch() && vault->exists(vault->existingCase(m.captured(1) + m.captured(2))))
                ++renamed;
        }
        if (kind == u"journal") {
            ++journals;
            if (appends)
                ++appended;
        } else if (kind == u"page") {
            ++pages;
        } else {
            ++attachments;
        }
        bytes += f.data.size();
        if (files.size() < 200)
            files.append(QVariantMap{ { u"path"_s, f.path }, { u"kind"_s, kind }, { u"append"_s, appends },
                                      { u"size"_s, f.data.size() } });
    }
    QStringList warnings = m_readWarnings;
    warnings.append(result.warnings);
    if (forImport) {
        m_writeQueue = result.files;
        m_writeWarnings = warnings;
        return;
    }
    const QString target = cleanTargetFolder(m_targetFolder).value_or(QString());
    m_preview = {
        { u"pages"_s, pages },
        { u"journals"_s, journals },
        { u"journalsAppended"_s, appended },
        { u"attachments"_s, attachments },
        { u"renamed"_s, renamed },
        { u"files"_s, files },
        { u"fileCount"_s, int(result.files.size()) },
        { u"bytes"_s, bytes },
        { u"sizeText"_s, sizeText(bytes) },
        { u"warnings"_s, warnings },
        { u"folder"_s, target },
        { u"folderExists"_s, !target.isEmpty() && vault->exists(target) },
    };
    Q_EMIT previewChanged();
    if (result.files.isEmpty()) {
        setState(u"error"_s, tr("Nothing to import was found in “%1”.").arg(m_sourceName));
        return;
    }
    setState(u"ready"_s);
}

// ---- Writing ---------------------------------------------------------------------------------------

void ImportController::startImport()
{
    if (m_state != u"ready" || !m_workspace || !m_workspace->vault() || m_preview.isEmpty())
        return;
    const io::Importer *importer = effectiveImporter();
    const std::optional<QString> folder = cleanTargetFolder(m_targetFolder);
    if (!importer || !folder)
        return;
    const int generation = ++m_generation;
    m_cancelRequested = false;
    m_writeSource = importer->name();
    m_writeFolder = *folder;
    m_writeCounts.clear();
    m_writeFailed.clear();
    m_firstPage.clear();
    m_writeIndex = 0;
    setState(u"importing"_s);
    setProgress(-1, tr("Preparing…"));

    // Planned again against the vault as it is now (it may have changed since the preview).
    io::ImportOptions options;
    options.config = m_workspace->vault()->config();
    options.exists = existsSnapshot();
    options.targetFolder = *folder;
    const QList<io::InputFile> input = m_input;
    runInBackground<io::ImportResult>(
            this, [importer, input, options] { return io::runImport(*importer, input, options); },
            [this, generation](io::ImportResult result) {
                if (generation != m_generation)
                    return;
                applyPlan(generation, result, true);
                writeStep();
            });
}

void ImportController::writeStep()
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault) {
        finishImport(true);
        return;
    }
    const VaultConfig config = vault->config();
    const auto count = [this](const QString &key) { m_writeCounts.insert(key, m_writeCounts.value(key).toInt() + 1); };
    const auto fail = [this, vault](const QString &path) {
        m_writeFailed.append(QVariantMap{ { u"path"_s, path }, { u"message"_s, vault->errorString() } });
    };
    int written = 0;
    while (m_writeIndex < m_writeQueue.size() && written < m_writeChunk && !m_cancelRequested) {
        const io::ImportedFile f = m_writeQueue.at(m_writeIndex++); // a copy: writing emits signals
        ++written;
        const QString kind = importedKind(f.path, f.append, config);
        if (f.append && vault->exists(f.path)) {
            bool ok = false;
            const QByteArray existing = vault->readBinary(f.path, &ok);
            if (!ok) {
                fail(f.path);
                continue;
            }
            const QByteArray merged = appendImportedJournal(existing, f.data, m_writeSource);
            if (merged != existing && !vault->writeBinary(f.path, merged, ChangeSource::Import)) {
                fail(f.path);
                continue;
            }
            count(u"journals"_s);
            count(u"appended"_s);
            continue;
        }
        // New files never replace anything: a path taken meanwhile gets a unique name.
        const VaultPath used = vault->createBinary(f.path, f.data, ChangeSource::Import);
        if (used.isEmpty()) {
            fail(f.path);
            continue;
        }
        // A folder existing in another letter case is used as spelled (Vault::create): no rename.
        if (used.compare(f.path, Qt::CaseInsensitive) != 0) {
            count(u"renamed"_s);
            m_writeWarnings.append(tr("“%1” already existed; it was saved as “%2”.").arg(f.path, used));
        }
        if (kind == u"journal") {
            count(u"journals"_s);
        } else if (kind == u"page") {
            count(u"pages"_s);
            if (m_firstPage.isEmpty())
                m_firstPage = used;
        } else {
            count(u"attachments"_s);
        }
    }
    const qsizetype total = m_writeQueue.size();
    setProgress(total > 0 ? qreal(m_writeIndex) / qreal(total) : 1,
                tr("Saving %1 of %2…").arg(m_writeIndex).arg(total));
    if (m_cancelRequested || m_writeIndex >= total) {
        finishImport(m_cancelRequested && m_writeIndex < total);
        return;
    }
    m_writeTimer.start();
}

void ImportController::finishImport(bool cancelled)
{
    m_writeTimer.stop();
    const int written = int(m_writeIndex) - int(m_writeFailed.size());
    m_report = {
        { u"pages"_s, m_writeCounts.value(u"pages"_s).toInt() },
        { u"journals"_s, m_writeCounts.value(u"journals"_s).toInt() },
        { u"appended"_s, m_writeCounts.value(u"appended"_s).toInt() },
        { u"attachments"_s, m_writeCounts.value(u"attachments"_s).toInt() },
        { u"renamed"_s, m_writeCounts.value(u"renamed"_s).toInt() },
        { u"written"_s, qMax(0, written) },
        { u"total"_s, int(m_writeQueue.size()) },
        { u"failed"_s, m_writeFailed },
        { u"warnings"_s, m_writeWarnings },
        { u"folder"_s, m_writeFolder },
        { u"firstPage"_s, m_firstPage },
        { u"source"_s, m_writeSource },
        { u"cancelled"_s, cancelled },
    };
    qCInfo(logging::lcIo).noquote()
            << u"Import from %1 %2: %3 of %4 file(s) saved into %5, %6 failed, %7 warning(s)"_s.arg(
                       m_writeSource, cancelled ? u"stopped"_s : u"finished"_s, QString::number(qMax(0, written)),
                       QString::number(m_writeQueue.size()), m_writeFolder.isEmpty() ? u"(notebook)"_s : m_writeFolder,
                       QString::number(m_writeFailed.size()), QString::number(m_writeWarnings.size()));
    m_writeQueue.clear();
    m_cancelRequested = false;
    Q_EMIT reportChanged();
    setProgress(1);
    setState(u"done"_s);
    Q_EMIT imported(cancelled ? tr("Import stopped: %n file(s) saved.", "", qMax(0, written))
                              : tr("Import finished: %n file(s) saved.", "", qMax(0, written)));
}

void ImportController::openTarget()
{
    if (!m_workspace)
        return;
    const QString folder = m_report.value(u"folder"_s, m_targetFolder).toString();
    if (!folder.isEmpty() && m_workspace->vault() && m_workspace->vault()->exists(folder)) {
        m_workspace->navigate({ { u"view"_s, u"allPages"_s }, { u"path"_s, folder } });
        return;
    }
    const QString first = m_report.value(u"firstPage"_s).toString();
    if (!first.isEmpty())
        openPage(first);
    else
        m_workspace->navigate({ { u"view"_s, u"journals"_s } });
}

void ImportController::openPage(const QString &path)
{
    if (m_workspace && !path.isEmpty())
        m_workspace->navigate({ { u"view"_s, u"page"_s }, { u"path"_s, path } });
}

} // namespace brulion::ioviews
