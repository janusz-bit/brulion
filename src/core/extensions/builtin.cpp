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
    // Read once: the palette asks for their titles per command, and the gym's catalog is 880
    // exercises with their instructions (immutable resources; a static local is initialised once,
    // thread-safely).
    static const QList<BuiltinExtension> builtins = [] {
        BuiltinExtension gym;
        gym.extension.id = u"silownia"_s;
        gym.extension.source = QString::fromUtf8(resource(u"silownia.luau"_s));
        gym.extension.permissions = {u"write"_s};
        // The catalog (scripts/gym-catalog.py): the exercises, and their instructions by id —
        // b.data("instructions", id) gives one exercise's (English; free-exercise-db's), and
        // b.data("instructions-pl", id) its Polish translation, written for the common ones only.
        gym.extension.data.insert(u"exercises"_s, QJsonDocument::fromJson(resource(u"exercises.json"_s)).toVariant());
        gym.extension.data.insert(u"instructions"_s,
                                  QJsonDocument::fromJson(resource(u"exercise-instructions.json"_s)).toVariant());
        gym.extension.data.insert(u"instructions-pl"_s,
                                  QJsonDocument::fromJson(resource(u"exercise-instructions-pl.json"_s)).toVariant());
        gym.titleEn = u"Gym"_s;
        gym.titlePl = u"Siłownia"_s;
        BuiltinExtension assistant;
        assistant.extension.id = u"asystent"_s;
        assistant.extension.source = QString::fromUtf8(resource(u"asystent.luau"_s));
        // (`app`: its tools for the app itself — settings, a theme, a button, a command —, offered
        // only where the app runs it and the model is on the network: ctx.appTools.)
        assistant.extension.permissions = {u"ai"_s, u"app"_s, u"write"_s};
        // The extensions' reference (docs/EXTENSIONS.md): the model reads it to write one.
        {
            QFile reference(u":/extensions/EXTENSIONS.md"_s);
            if (reference.open(QIODevice::ReadOnly))
                assistant.extension.data.insert(u"reference"_s, QString::fromUtf8(reference.readAll()));
        }
        assistant.titleEn = u"AI assistant"_s;
        assistant.titlePl = u"Asystent AI"_s;
        BuiltinExtension habits;
        habits.extension.id = u"nawyki"_s;
        habits.extension.source = QString::fromUtf8(resource(u"nawyki.luau"_s));
        habits.extension.permissions = {u"write"_s};
        habits.titleEn = u"Habits"_s;
        habits.titlePl = u"Nawyki"_s;
        BuiltinExtension cards;
        cards.extension.id = u"fiszki"_s;
        cards.extension.source = QString::fromUtf8(resource(u"fiszki.luau"_s));
        cards.extension.permissions = {u"write"_s};
        cards.titleEn = u"Flashcards"_s;
        cards.titlePl = u"Fiszki"_s;
        return QList<BuiltinExtension>{gym, assistant, habits, cards};
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
