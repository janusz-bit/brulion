#include "notebookdictionary.h"

#include "appsettings.h"
#include "workspace.h"

#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <algorithm>

namespace brulion::shell {

NotebookDictionary::NotebookDictionary(Workspace *workspace, AppSettings *settings, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_settings(settings)
{
    connect(workspace, &Workspace::openChanged, this, &NotebookDictionary::notebookOpenedOrClosed);
    connect(settings, &AppSettings::spellingWordsChanged, this, [this] {
        if (!openVault())
            take(deviceWords());
    });
    notebookOpenedOrClosed();
}

Vault *NotebookDictionary::openVault() const
{
    return m_workspace && m_workspace->isOpen() ? m_workspace->vault() : nullptr;
}

QStringList NotebookDictionary::deviceWords() const
{
    return m_settings ? parseDictionary(m_settings->spellingWords().join(u'\n')) : QStringList();
}

void NotebookDictionary::setWords(const QStringList &words)
{
    Vault *vault = openVault();
    if (!vault) {
        if (m_settings)
            m_settings->setSpellingWords(words);
        return;
    }
    const QStringList clean = parseDictionary(words.join(u'\n'));
    if (clean == m_words)
        return;
    vault->writeText(vaultDictionaryPath(), serializeDictionary(clean));
    take(clean);
}

void NotebookDictionary::notebookOpenedOrClosed()
{
    Vault *vault = openVault();
    if (!vault) {
        take(deviceWords());
        return;
    }
    connect(vault, &Vault::changed, this, &NotebookDictionary::vaultChanged, Qt::UniqueConnection);
    if (!vault->exists(vaultDictionaryPath())) {
        // A notebook's first dictionary: the words this device had for every notebook.
        const QStringList seed = deviceWords();
        if (!seed.isEmpty())
            vault->writeText(vaultDictionaryPath(), serializeDictionary(seed));
        take(seed);
        return;
    }
    readNotebook();
}

void NotebookDictionary::vaultChanged(const QList<VaultChange> &changes)
{
    const bool touched = std::ranges::any_of(changes, [](const VaultChange &c) {
        return c.path == vaultDictionaryPath() || c.oldPath == vaultDictionaryPath();
    });
    if (touched)
        readNotebook();
}

void NotebookDictionary::readNotebook()
{
    Vault *vault = openVault();
    if (!vault)
        return;
    if (!vault->exists(vaultDictionaryPath())) {
        take({}); // (deleted: a dictionary with no words)
        return;
    }
    bool ok = false;
    const QString text = vault->readText(vaultDictionaryPath(), &ok);
    if (ok) // (a read that failed is no empty dictionary: the next word added would drop the others)
        take(parseDictionary(text));
}

void NotebookDictionary::take(const QStringList &words)
{
    if (words == m_words)
        return;
    m_words = words;
    Q_EMIT wordsChanged();
}

} // namespace brulion::shell
