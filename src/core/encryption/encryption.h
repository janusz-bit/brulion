// brulion::encryption — encrypted notes (docs/SYNTAX.md §11 "Encrypted text", docs/UI.md "Encrypted
// notes", docs/ARCHITECTURE.md "Security"): the formats of Obsidian's Meld Encrypt plugin, read in
// every version it ever wrote and written in its current one, so one notebook opens in both apps.
// GUI-free and pure (randomness and time are given), any thread; the cryptography is core `crypto`'s.
//
//   meld.h      the formats: decryptPayload / encryptPayload (an inline fragment's Base64: v0
//               "obsolete", v1 α, v2 β read; v3 γ read and written), readNoteFile / decryptNote /
//               writeNoteFile (a whole `.mdenc` note: JSON 1.0, 2.0 read, 3.0 read and written,
//               byte for byte as the plugin's JSON.stringify(…, null, 2) writes it), Failure
//   fragments.h the edits of a note's text: encryptFragment (plaintext → `%%🔐γ … 🔐%%`),
//               decryptFragment, reencryptFragment (edited text under the same password, hint and
//               visibility; an old version becomes γ), changeFragmentPassword
//   keyring.h   Keyring — the passwords of this session (in wiped memory), each remembered for the
//               page it unlocked, all forgotten after a time without use
//
// Security rules every caller keeps (docs/ARCHITECTURE.md "Security"): decrypted text lives only
// in memory — never written to a file, the index, a log, the history or a cache —, a password is
// registered with logging::ScopedSecret while it is held, and nothing decrypts by itself: only a
// user's action tries remembered passwords, at most two per fragment (each try costs a key
// derivation of up to two million rounds, which a hostile synced note could ask for).
#pragma once

#include "encryption/fragments.h"
#include "encryption/keyring.h"
#include "encryption/meld.h"
