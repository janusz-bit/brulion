#include "logexport.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace brulion::diag {

namespace {

QString utcOffset(const QDateTime &time)
{
    const int seconds = time.offsetFromUtc();
    const int minutes = qAbs(seconds) / 60;
    return u"UTC%1%2:%3"_s.arg(seconds < 0 ? u'-' : u'+')
            .arg(minutes / 60, 2, 10, QChar(u'0'))
            .arg(minutes % 60, 2, 10, QChar(u'0'));
}

} // namespace

QStringList exportHeader(const ExportInfo &info)
{
    QStringList lines;
    lines.append(QCoreApplication::translate("brulion::diag::LogExport", "Brulion — event log"));
    lines.append(QCoreApplication::translate("brulion::diag::LogExport", "Version %1 · %2 · Qt %3")
                         .arg(info.version, info.device, info.qtVersion));
    lines.append(QCoreApplication::translate("brulion::diag::LogExport", "Created %1 (%2) · detail: %3")
                         .arg(info.created.toString(u"yyyy-MM-dd HH:mm:ss"_s), utcOffset(info.created),
                              info.detailed ? QCoreApplication::translate("brulion::diag::LogExport", "detailed")
                                            : QCoreApplication::translate("brulion::diag::LogExport", "normal")));
    lines.append(QCoreApplication::translate(
            "brulion::diag::LogExport",
            "Contains: the Nextcloud server address and user name, names and paths of the notes that "
            "changes and errors concern, the notebook folder, device and app versions."));
    lines.append(QCoreApplication::translate(
            "brulion::diag::LogExport",
            "Does not contain: passwords, app passwords, tokens, cookies or note contents — Brulion removes "
            "them before anything is written to the log."));
    lines.append(QCoreApplication::translate("brulion::diag::LogExport",
                                             "The log stays on this device; Brulion never sends it anywhere."));
    if (!info.recentSyncs.isEmpty()) {
        lines.append(QCoreApplication::translate("brulion::diag::LogExport", "Recent syncs:"));
        for (const QString &line : info.recentSyncs)
            lines.append(u"  "_s + line);
    }
    return lines;
}

QString suggestedFileName(const QDateTime &now)
{
    //: File name stem of an exported event log (ASCII letters, digits and dashes only).
    return QCoreApplication::translate("brulion::diag::LogExport", "brulion-event-log") + u'-'
           + now.toString(u"yyyy-MM-dd-HHmm"_s) + u".txt"_s;
}

QString capForClipboard(const QString &text, qint64 maxBytes)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= maxBytes)
        return text;
    const qsizetype separator = utf8.indexOf("\n----\n");
    const QByteArray head = separator >= 0 ? utf8.left(separator + 6) : QByteArray();
    const QByteArray note =
            QCoreApplication::translate("brulion::diag::LogExport",
                                        "(older entries left out — use “Save” or “Share” for the whole log)")
                    .toUtf8()
            + '\n';
    const qint64 room = qMax<qint64>(0, maxBytes - head.size() - note.size());
    qsizetype cut = utf8.size() - room;
    // Start at an entry: after a newline, not at a continuation line ("\t…").
    while (cut < utf8.size()) {
        const qsizetype nl = utf8.indexOf('\n', cut);
        if (nl < 0) {
            cut = utf8.size();
            break;
        }
        cut = nl + 1;
        if (cut < utf8.size() && utf8.at(cut) != '\t')
            break;
    }
    if (cut < head.size())
        cut = head.size();
    return QString::fromUtf8(head + note + utf8.mid(cut));
}

} // namespace brulion::diag
