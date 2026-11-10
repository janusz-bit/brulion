// Spell checking with Android's own spell checker (spellbackends.h): the keyboard's dictionaries
// through TextServicesManager / SpellCheckerSession — JNI to
// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt (spellLanguages, spellOpen,
// spellCheck, spellSuggest → SpellCheck.kt). Nothing added to the APK. Words travel as JSON (one
// call per batch: the checker answers over IPC); each call waits for the checker at most a few
// seconds (no answer: the words count as right, nothing is underlined). Any thread: QJniObject
// attaches it and finds the class through the app's class loader.
#include "spellbackends.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

constexpr char kClass[] = "io/github/januszbit/brulion/android/BrulionAndroid";

bool threw()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions();
}

QJniObject context()
{
    return QNativeInterface::QAndroidApplication::context();
}

QJsonArray jsonArray(const QJniObject &answer)
{
    if (threw() || !answer.isValid())
        return {};
    return QJsonDocument::fromJson(answer.toString().toUtf8()).array();
}

class AndroidSpellBackend final : public spell::SpellBackend {
public:
    explicit AndroidSpellBackend(QString language) : m_language(std::move(language)) { }

    QString language() const override { return m_language; }

    bool check(const QString &word) override { return checkAll({word}).value(0, true); }

    QList<bool> checkAll(const QStringList &words) override
    {
        QList<bool> out(words.size(), true);
        if (words.isEmpty())
            return out;
        const QString json =
                QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(words)).toJson(QJsonDocument::Compact));
        const QJsonArray answer = jsonArray(QJniObject::callStaticObjectMethod(
                kClass, "spellCheck",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", context().object(),
                QJniObject::fromString(m_language).object<jstring>(), QJniObject::fromString(json).object<jstring>()));
        for (qsizetype i = 0; i < out.size() && i < answer.size(); ++i)
            out[i] = answer.at(i).toBool(true);
        return out;
    }

    QStringList suggest(const QString &word, int max) override
    {
        const QJsonArray answer = jsonArray(QJniObject::callStaticObjectMethod(
                kClass, "spellSuggest",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;",
                context().object(), QJniObject::fromString(m_language).object<jstring>(),
                QJniObject::fromString(word).object<jstring>(), jint(max)));
        QStringList out;
        for (const QJsonValue &v : answer) {
            if (v.isString() && out.size() < max)
                out.append(v.toString());
        }
        return out;
    }

private:
    QString m_language;
};

} // namespace

QString spellEngine()
{
    return u"android"_s;
}

QStringList spellLanguages()
{
    const QJniObject c = context();
    if (!c.isValid())
        return {};
    const QJsonArray languages = jsonArray(QJniObject::callStaticObjectMethod(
            kClass, "spellLanguages", "(Landroid/content/Context;)Ljava/lang/String;", c.object()));
    QStringList out;
    for (const QJsonValue &v : languages) {
        if (const QString language = v.toString(); !language.isEmpty()) // ("" for a non-string)
            out.append(language);
    }
    return out;
}

std::unique_ptr<spell::SpellBackend> createSpellBackend(const QString &language)
{
    const QJniObject c = context();
    if (!c.isValid())
        return nullptr;
    const jboolean open = QJniObject::callStaticMethod<jboolean>(
            kClass, "spellOpen", "(Landroid/content/Context;Ljava/lang/String;)Z", c.object(),
            QJniObject::fromString(language).object<jstring>());
    if (threw() || !open)
        return nullptr;
    return std::make_unique<AndroidSpellBackend>(language);
}

} // namespace brulion::platform
