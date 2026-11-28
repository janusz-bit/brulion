// NotebookDictionary — the personal dictionary the spell checker takes (docs/UI.md "Spell
// checking"): the open notebook's (.brulion/dictionary.txt, one word per line, synced between
// devices as a set of words: sync::mergeWordSet3), the device's list (AppSettings `spelling/words`)
// while no notebook is open.
//
// A notebook without the file starts from the device's list, written into it at once so other
// devices get it too (an empty file is a dictionary with no words: never filled again). The file
// changed behind the app's back — a sync, Syncthing, another app — is read again when the vault
// reports it.
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

namespace brulion {
class Vault;
struct VaultChange;
} // namespace brulion

namespace brulion::shell {

class AppSettings;
class Workspace;

class NotebookDictionary : public QObject {
    Q_OBJECT

public:
    NotebookDictionary(Workspace *workspace, AppSettings *settings, QObject *parent = nullptr);

    /// Sorted by code point, no duplicates.
    QStringList words() const { return m_words; }
    /// The user's list after an edit (added or removed words): into the notebook's file when one is
    /// open, else the device's list.
    void setWords(const QStringList &words);

Q_SIGNALS:
    void wordsChanged();

private:
    Vault *openVault() const;
    QStringList deviceWords() const;
    void notebookOpenedOrClosed();
    void vaultChanged(const QList<brulion::VaultChange> &changes);
    void readNotebook();
    void take(const QStringList &words);

    QPointer<Workspace> m_workspace;
    QPointer<AppSettings> m_settings;
    QStringList m_words;
};

} // namespace brulion::shell
