#include "wasmselftest.h"

#include "wasmfiles.h"
#include "wasmnotifier.h"
#include "wasmstorage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <emscripten/em_js.h>

#include <functional>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

// JavaScript in EM_JS: semicolons everywhere, double quotes only (see wasmstorage.cpp).
EM_JS_DEPS(brulion_wasm_selftest, "$UTF8ToString");

// The shell's brulionShell.selftest(key, text) also sends it to the test's server when the page
// was opened with ?report=<path> (browsers driven without DevTools: scripts/android-emulator-test.sh
// --web-keyboard).
EM_JS(void, brulion_selftest_publish, (const char *name, const char *json), {
    var key = UTF8ToString(name);
    var text = UTF8ToString(json);
    globalThis[key] = JSON.parse(text);
    console.log("BRULION_SELFTEST " + key + " " + text);
    var shell = globalThis["brulionShell"];
    if (shell && shell.selftest) {
        try {
            shell.selftest(key, text);
        } catch (e) {
            console.error(e);
        }
    }
});

namespace brulion::platform {

void publishSelfTestResult(const char *name, const QJsonObject &result)
{
    brulion_selftest_publish(name, QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
}

void runStorageSelfTest(WasmStorage *storage)
{
    QJsonObject result{
        {u"home"_s, QDir::homePath()},
        {u"vaultRoot"_s, WasmStorage::vaultRoot()},
        {u"appData"_s, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)},
        {u"mounts"_s, QJsonArray::fromStringList(storage->mountPoints())},
        {u"cacheMounts"_s, QJsonArray::fromStringList(storage->cacheMountPoints())},
        {u"backed"_s, storage->isBackedByIndexedDb()},
    };

    QJsonArray counters;
    bool written = true;
    for (const QString &mount : storage->mountPoints() + storage->cacheMountPoints()) {
        const QString path = mount + u"/.brulion-selftest"_s;
        int previous = 0;
        QFile in(path);
        if (in.open(QIODevice::ReadOnly))
            previous = QString::fromUtf8(in.readAll()).trimmed().toInt();
        in.close();
        QSaveFile out(path);
        const bool ok = out.open(QIODevice::WriteOnly)
            && out.write(QByteArray::number(previous + 1)) >= 0 && out.commit();
        written = written && ok;
        counters.append(QJsonObject{
            {u"path"_s, path},
            {u"previous"_s, previous},
            {u"now"_s, previous + 1},
            {u"written"_s, ok},
        });
    }
    result.insert(u"counters"_s, counters);
    result.insert(u"written"_s, written);

    // Publish once: after the automatic write-back, or after a timeout if none happens.
    auto published = std::make_shared<bool>(false);
    auto publish = [result, published](bool flushed, const QString &note) mutable {
        if (*published)
            return;
        *published = true;
        result.insert(u"flushed"_s, flushed);
        if (!note.isEmpty())
            result.insert(u"note"_s, note);
        brulion_selftest_publish("brulionSelfTest",
                                 QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    };

    if (!storage->isBackedByIndexedDb()) {
        publish(false, u"IndexedDB unavailable: "_s + storage->lastError());
        return;
    }
    QObject::connect(storage, &WasmStorage::flushed, storage,
                     [publish](bool ok) mutable { publish(ok, QString()); },
                     Qt::SingleShotConnection);
    QTimer::singleShot(10000, storage, [publish]() mutable {
        publish(false, u"no automatic write-back within 10 s"_s);
    });
}

namespace {

/// Calls `action` once, at the next mouse button release or touch end anywhere in the app.
class OnNextRelease : public QObject {
public:
    explicit OnNextRelease(std::function<void()> action)
        : QObject(QCoreApplication::instance()), m_action(std::move(action))
    {
        QCoreApplication::instance()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (m_action && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::TouchEnd)) {
            const std::function<void()> action = std::exchange(m_action, {});
            deleteLater();
            action();
        }
        return false;
    }

private:
    std::function<void()> m_action;
};

void startSaveSelfTest()
{
    new OnNextRelease([] {
        wasm::saveFile(QByteArray("\xc4\x85\xc4\x99\n"), u"Zażółć.md"_s); // "ąę\n"
        brulion_selftest_publish("brulionSaveSelfTest", R"({"started":true})");
    });
}

} // namespace

void runFilePickerSelfTest(bool pick, bool save)
{
    if (!pick) {
        if (save)
            startSaveSelfTest();
        return;
    }
    new OnNextRelease([save] {
        wasm::openFiles({ u"Notatki (*.md *.txt)"_s }, true, 1024 * 1024, [save](const QList<PickedFile> &picked) {
            QJsonArray files;
            for (const PickedFile &f : picked) {
                files.append(QJsonObject{
                        {u"name"_s, f.name},
                        {u"size"_s, f.size},
                        {u"tooLarge"_s, f.tooLarge},
                        {u"read"_s, qint64(f.data.size())},
                        {u"sha256"_s, f.tooLarge ? QString()
                                                 : QString::fromLatin1(QCryptographicHash::hash(
                                                           f.data, QCryptographicHash::Sha256).toHex())},
                });
            }
            publishSelfTestResult("brulionPickSelfTest",
                                  QJsonObject{ { u"cancelled"_s, picked.isEmpty() }, { u"files"_s, files } });
            if (save)
                startSaveSelfTest();
        });
    });
}

void runNotifierSelfTest()
{
    auto *notifier = new WasmNotifier(QCoreApplication::instance());
    const auto permissionName = [](WasmNotifier::Permission p) {
        return QString::fromLatin1(QMetaEnum::fromType<WasmNotifier::Permission>().valueToKey(
                                       static_cast<int>(p)))
            .toLower();
    };
    auto result = std::make_shared<QJsonObject>();
    const auto publish = [result] {
        brulion_selftest_publish("brulionNotifySelfTest",
                                 QJsonDocument(*result).toJson(QJsonDocument::Compact).constData());
    };

    const WasmNotifier::Permission permission = notifier->permission();
    result->insert(u"permission"_s, permissionName(permission));
    bool shown = false;
    if (permission == WasmNotifier::Permission::Granted) {
        QObject::connect(notifier, &WasmNotifier::activated, notifier,
                         [result, publish](const QString &id, const QString &path, int line) {
                             result->insert(u"activated"_s, QJsonObject{
                                                                {u"id"_s, id},
                                                                {u"path"_s, path},
                                                                {u"line"_s, line},
                                                            });
                             publish();
                         });
        shown = notifier->showNow(u"selftest"_s, u"Brulion"_s,
                                  u"Przypomnienie testowe — zażółć gęślą jaźń"_s,
                                  u"journals/2026-10-14.md"_s, 3, false);
    }
    result->insert(u"shown"_s, shown);
    publish();
}

} // namespace brulion::platform
