#include "extensions/builtin.h"

#include "extensions/pageextension.h"

#include <QFile>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

QByteArray resource(const QString &name)
{
    QFile file(u":/extensions/"_s + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

QList<BuiltinExtension> builtinExtensions()
{
    // Read once: the palette asks for their titles per command, and the catalog is 1000 lines
    // of JSON (immutable resources; a static local is initialised once, thread-safely).
    static const QList<BuiltinExtension> builtins = [] {
        BuiltinExtension gym;
        gym.extension.id = u"silownia"_s;
        gym.extension.source = QString::fromUtf8(resource(u"silownia.luau"_s));
        gym.extension.permissions = {u"write"_s};
        gym.extension.data.insert(u"exercises"_s, QJsonDocument::fromJson(resource(u"exercises.json"_s)).toVariant());
        gym.titleEn = u"Gym"_s;
        gym.titlePl = u"Siłownia"_s;
        BuiltinExtension assistant;
        assistant.extension.id = u"asystent"_s;
        assistant.extension.source = QString::fromUtf8(resource(u"asystent.luau"_s));
        assistant.extension.permissions = {u"ai"_s, u"write"_s};
        // The extensions' reference (docs/EXTENSIONS.md): the model reads it to write one.
        {
            QFile reference(u":/extensions/EXTENSIONS.md"_s);
            if (reference.open(QIODevice::ReadOnly))
                assistant.extension.data.insert(u"reference"_s, QString::fromUtf8(reference.readAll()));
        }
        assistant.titleEn = u"AI assistant"_s;
        assistant.titlePl = u"Asystent AI"_s;
        return QList<BuiltinExtension>{gym, assistant};
    }();
    return builtins;
}

QString builtinFor(const QString &pageId)
{
    for (const BuiltinExtension &b : builtinExtensions())
        if (b.extension.id == pageId || extensionId(b.titleEn) == pageId || extensionId(b.titlePl) == pageId)
            return b.extension.id;
    return {};
}

Extension builtinExtension(const QString &id)
{
    for (const BuiltinExtension &b : builtinExtensions())
        if (b.extension.id == id)
            return b.extension;
    return {};
}

} // namespace brulion::extensions
