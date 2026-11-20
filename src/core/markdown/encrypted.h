// Encrypted text in notes (docs/SYNTAX.md §11 "Encrypted text"): the markers of Obsidian's Meld
// Encrypt plugin, as syntax only — where a fragment is, its version, its public hint and its Base64
// payload. Decrypting and encrypting are the `encryption` module's (core, over `crypto`); the
// markdown module only finds the fragments (the renderer shows them as locked chips, the search
// index leaves their payloads out). Part of the markdown module — see markdown/markdown.h.
//
//   %%🔐γ 💡hint💡<Base64> 🔐%%     hidden: an Obsidian comment (its reading view shows nothing)
//   🔐γ 💡hint💡<Base64> 🔐         visible: Obsidian's reading view shows a 🔐 to click
//
// γ (gamma) is the current format (Meld Encrypt 3.0, AES-256-GCM, PBKDF2-SHA512, authenticated
// parameters), β (beta) and α (alpha) older ones, a bare `🔐 ` the oldest. The hint (`💡…💡`) is
// optional. A fragment never spans lines.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// The format of an encrypted fragment, by its marker (Meld Encrypt's Decryptable.version).
enum class EncryptedVersion : quint8 {
    Obsolete = 0, ///< `%%🔐 ` / `🔐 `: AES-GCM with SHA-256(password) as key and a fixed IV (read only)
    Alpha = 1,    ///< `%%🔐α `: PBKDF2-SHA256, 1000 rounds, a fixed salt (read only)
    Beta = 2,     ///< `%%🔐β `: PBKDF2-SHA512, 210 000 rounds, random salt and 16-byte IV (read only)
    Gamma = 3,    ///< `%%🔐γ `: v3 — suite, rounds, salt, IV and ciphertext in one payload (written)
};

/// One encrypted fragment of a text (findEncryptedFragments()). Ranges are UTF-16 offsets.
struct EncryptedSpan {
    qsizetype from = 0;        ///< the fragment with its markers: from the `%%` or the first 🔐 …
    qsizetype to = 0;          ///< … to after the closing 🔐 (or `%%`)
    qsizetype payloadFrom = 0; ///< the Base64 payload
    qsizetype payloadTo = 0;
    EncryptedVersion version = EncryptedVersion::Gamma;
    bool hidden = false; ///< written as an Obsidian comment `%%…%%`
    QString hint;        ///< the public hint ("" none)

    friend bool operator==(const EncryptedSpan &, const EncryptedSpan &) = default;
};

/// The marker emoji (U+1F510) and the hint's (U+1F4A1).
inline constexpr QStringView EncryptedIcon = u"\U0001F510";
inline constexpr QStringView EncryptedHintMark = u"\U0001F4A1";

/// The encrypted fragments of `text` in order, as Meld Encrypt reads them: a prefix — the hidden
/// one (`%%🔐γ `) before the visible one (`🔐γ `), γ, β, α, then the bare `🔐 ` —, an optional
/// `💡hint💡`, a payload of Base64 characters at least as long as that version's smallest
/// ciphertext (so "🔐 hello 🔐" is no fragment), and the matching suffix (` 🔐%%` after a hidden
/// prefix, ` 🔐` after a visible one), all on one line, with no 🔐 between them. Code spans and
/// fences are not looked at: a renderer masks them first. Linear time; malformed markers are text.
QList<EncryptedSpan> findEncryptedFragments(QStringView text);

/// Does `text` hold a 🔐 at all (a cheap test before findEncryptedFragments())?
bool mayContainEncrypted(QStringView text);

/// The fragment at exactly [from, to) of `text`, if there is one.
std::optional<EncryptedSpan> encryptedFragmentAt(QStringView text, qsizetype from, qsizetype to);

/// The text of a fragment: `version`'s prefix (hidden or visible), the hint between 💡 when there
/// is one, the payload, the suffix — Meld Encrypt's encodeEncryption().
QString encryptedFragmentText(EncryptedVersion version, bool hidden, QStringView hint, QStringView payload);

/// May `hint` be written into a fragment: no 🔐, no 💡 and no line break (Meld Encrypt refuses
/// the markers; a line break would end the fragment).
bool isValidEncryptedHint(QStringView hint);

/// `text` with every fragment blanked (spaces; line breaks kept, the length unchanged): what the
/// full-text index reads, so ciphertext makes no words.
QString maskEncryptedFragments(QStringView text);

/// A whole encrypted note of Meld Encrypt: `.mdenc` (also its older `.encrypted`), any letter case.
bool isEncryptedNotePath(QStringView path);
/// The extension Brulion gives a new encrypted note ("mdenc").
inline constexpr QStringView EncryptedNoteExtension = u"mdenc";

} // namespace brulion::markdown
