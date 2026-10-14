#include "sync/conflicts.h"

#include "util/paths.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// "dir/Name.ext" -> {"dir/Name", ".ext"}; no extension (or a leading-dot name) -> {path, ""}.
std::pair<QString, QString> splitExtension(const VaultPath &path)
{
    const QString base = paths::basename(path);
    const qsizetype dot = base.lastIndexOf(u'.');
    if (dot <= 0)
        return {path, QString()};
    const QString ext = base.mid(dot);
    return {path.left(path.size() - ext.size()), ext};
}

} // namespace

QString conflictStamp(const QDateTime &time)
{
    return time.toLocalTime().toString(u"yyyyMMdd-HHmmss"_s);
}

QString sanitizeDeviceName(const QString &device)
{
    static const QRegularExpression unsafe(u"[^\\p{L}\\p{N}_-]+"_s);
    static const QRegularExpression dashes(u"-{2,}"_s);
    static const QRegularExpression edgeDashes(u"^-+|-+$"_s);
    static const QRegularExpression trailingDashes(u"-+$"_s);
    QString cleaned = device.normalized(QString::NormalizationForm_C);
    cleaned.replace(unsafe, u"-"_s);
    cleaned.replace(dashes, u"-"_s);
    cleaned.remove(edgeDashes);
    cleaned = cleaned.left(40);
    if (!cleaned.isEmpty() && cleaned.back().isHighSurrogate())
        cleaned.chop(1); // never split a letter outside the BMP (a lone surrogate)
    cleaned.remove(trailingDashes);
    return cleaned.isEmpty() ? u"urzadzenie"_s : cleaned;
}

VaultPath conflictCopyPath(const VaultPath &path, const QString &device, const QDateTime &time)
{
    const auto [stem, ext] = splitExtension(path);
    return stem + u".conflict-"_s + sanitizeDeviceName(device) + u'-' + conflictStamp(time) + ext;
}

std::optional<BrulionConflict> parseBrulionConflict(const VaultPath &path)
{
    static const QRegularExpression re(u"^(.*)\\.conflict-(.+)-(\\d{8})-(\\d{6})(?: \\d+)?(\\.[^./]*)?$"_s);
    const QRegularExpressionMatch m = re.match(paths::basename(path));
    if (!m.hasMatch() || m.capturedLength(1) == 0)
        return std::nullopt;
    const QString d = m.captured(3);
    const QString t = m.captured(4);
    BrulionConflict out;
    out.original = paths::join(paths::dirname(path), m.captured(1) + m.captured(5));
    out.time = d.left(4) + u'-' + d.mid(4, 2) + u'-' + d.mid(6, 2) + u'T' + t.left(2) + u':' + t.mid(2, 2) + u':'
        + t.mid(4, 2);
    out.device = m.captured(2);
    return out;
}

QList<ConflictCopy> findConflicts(const QList<FileStat> &files)
{
    QList<ConflictCopy> out;
    for (const FileStat &file : files) {
        if (file.isDir)
            continue;
        if (const std::optional<paths::SyncthingConflict> st = paths::parseSyncthingConflict(file.path)) {
            out.append(ConflictCopy{st->original, file.path, st->time, st->device, ConflictCopy::Source::Syncthing});
            continue;
        }
        if (const std::optional<BrulionConflict> own = parseBrulionConflict(file.path))
            out.append(ConflictCopy{own->original, file.path, own->time, own->device, ConflictCopy::Source::Brulion});
    }
    std::stable_sort(out.begin(), out.end(), [](const ConflictCopy &a, const ConflictCopy &b) {
        return a.original != b.original ? a.original < b.original : a.time < b.time;
    });
    return out;
}

} // namespace brulion::sync
