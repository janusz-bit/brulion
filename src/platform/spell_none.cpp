// No spell checking in this build (spellbackends.h): the browser, and desktop builds without
// Hunspell. The app hides spell checking (no languages).
#include "spellbackends.h"

namespace brulion::platform {

QString spellEngine()
{
    return {};
}

QStringList spellLanguages()
{
    return {};
}

std::unique_ptr<spell::SpellBackend> createSpellBackend(const QString &)
{
    return nullptr;
}

} // namespace brulion::platform
