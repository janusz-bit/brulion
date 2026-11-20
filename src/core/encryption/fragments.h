// brulion::encryption — encrypted fragments of a note's text (see encryption/encryption.h): the pure
// edits behind the editor's "Encrypt selection", the unlocked fragment's "Save", "Decrypt
// permanently" and "Change password". Content in → content out; the rest of the text is never
// touched.
#pragma once

#include "encryption/meld.h"
#include "markdown/encrypted.h"

#include <QString>
#include <QStringView>

namespace brulion::encryption {

/// A new fragment (always v3, γ) holding `plaintext` under `password`: hidden `%%🔐γ … 🔐%%` (an
/// Obsidian comment) or visible `🔐γ … 🔐`, with the public `hint` when it is not empty. Empty
/// when the hint is not allowed (markdown::isValidEncryptedHint()).
QString encryptFragment(QStringView plaintext, const crypto::SecureBytes &password, QStringView hint = {},
                        bool hidden = true, const crypto::RandomSource &random = {});

/// The plaintext of fragment `span` of `text`.
Decrypted decryptFragment(QStringView text, const markdown::EncryptedSpan &span, const crypto::SecureBytes &password);

/// The fragment `span` again with `plaintext` under the same `password`: its hint and visibility
/// kept, its version made γ (an old one is upgraded, as the plugin does when its dialog saves).
QString reencryptFragment(const markdown::EncryptedSpan &span, QStringView plaintext,
                          const crypto::SecureBytes &password, const crypto::RandomSource &random = {});

/// `text` with [from, to) replaced by `replacement` — the edits' common last step.
QString replaceRange(QStringView text, qsizetype from, qsizetype to, QStringView replacement);

/// Fragment `span` of `text` under a new password (and hint): the text with that fragment
/// replaced, or nullopt with `failure` when `oldPassword` does not open it.
std::optional<QString> changeFragmentPassword(QStringView text, const markdown::EncryptedSpan &span,
                                              const crypto::SecureBytes &oldPassword,
                                              const crypto::SecureBytes &newPassword, QStringView newHint,
                                              Failure *failure = nullptr, const crypto::RandomSource &random = {});

/// Fragment `span` of `text` decrypted in place ("Decrypt permanently"): the text with the
/// plaintext where the fragment was, or nullopt with `failure`.
std::optional<QString> decryptInPlace(QStringView text, const markdown::EncryptedSpan &span,
                                      const crypto::SecureBytes &password, Failure *failure = nullptr);

} // namespace brulion::encryption
