// The notebook's attachments as the device's text cache knows them: by path, the size and
// modification time a file had when its content hash was taken. A file whose size and time are
// unchanged keeps its hash (it is not read again); a renamed one keeps its record. Part of the
// attachments module — see attachments/attachments.h.
//
// The file's format (little-endian): "BRAM"  u32 ManifestFormatVersion  u32 count
//   count × { u32 path's length + UTF-8  i64 size  i64 mtime (ms)  32 bytes SHA-256 }
//   u32 FNV-1a 32 of everything before it.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QString>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace brulion::attachments {

inline constexpr quint32 ManifestFormatVersion = 1;
/// The most entries a manifest file holds (a hostile file's count).
inline constexpr quint32 MaxManifestEntries = 1'000'000;

struct ManifestEntry {
    qint64 size = 0;
    /// Modification time, ms since the epoch (FileStat::mtime).
    qint64 mtime = 0;
    /// SHA-256 of the file's bytes (32 bytes).
    QByteArray hash;
    friend bool operator==(const ManifestEntry &, const ManifestEntry &) = default;
};

/// Vault path → entry.
using Manifest = QHash<QString, ManifestEntry>;

QByteArray encodeManifest(const Manifest &manifest);
/// Nullopt for anything malformed (the cache then starts empty).
std::optional<Manifest> decodeManifest(QByteArrayView bytes);

/// SHA-256 of what `device` holds from its current position to its end, read in 1 MiB chunks;
/// nullopt when reading fails or `cancelled` (asked between chunks) says so.
std::optional<QByteArray> hashFile(QIODevice &device, const std::function<bool()> &cancelled = {});

/// The record file's name for a hash: its 64 hexadecimal digits and ".bat".
QString recordFileName(const QByteArray &hash);
/// The manifest's file of the notebook `vaultId` (Vault::id()) in the cache's folder:
/// "notebooks/<20 hex digits of its SHA-1>.manifest" — the app writes it, brulion-cli reads it.
QString manifestFileName(const QString &vaultId);

} // namespace brulion::attachments
