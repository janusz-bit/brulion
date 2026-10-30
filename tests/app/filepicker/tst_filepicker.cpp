// FilePicker (src/app/ui/filepicker.h): the system's file picker for QML instead of QtQuick.Dialogs.
// The pure helpers (the default suffix, the name filters) data-driven; the picker's life with fake
// backends (what each mode asks for, accept / cancel / reject, signals in order, open while open,
// destroyed while open, the first backend that shows wins, none at all); the parent window from
// the QQuickItem it belongs to; the QML API (the enum, list<url>); and, in desktop builds, the
// widget fallback (QFileDialog) offscreen: picked for real, cancelled, the dark palette.
#include "filepicker.h"
#include "filepicker_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqml.h>

#ifdef BRULION_FILEPICKER_WIDGETS
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#else
#include <QGuiApplication>
#endif

#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using brulion::ui::FilePicker;
using brulion::ui::FilePickerBackend;
using brulion::ui::FilePickerRequest;

namespace {

/// Records what it was asked to show; accepts or rejects when the test says so.
class FakeBackend : public FilePickerBackend {
    Q_OBJECT

public:
    explicit FakeBackend(bool canShow = true) : m_canShow(canShow) { }
    bool show(const FilePickerRequest &request, QWindow *parent) override
    {
        ++shown;
        lastRequest = request;
        lastParent = parent;
        return m_canShow;
    }
    void hide() override { ++hidden; }

    int shown = 0;
    int hidden = 0;
    FilePickerRequest lastRequest;
    QWindow *lastParent = nullptr;

private:
    bool m_canShow;
};

/// The backends the next open() gets (the factory hands them over; the test keeps QPointers).
struct Fakes {
    QList<bool> canShow{true};
    QList<QPointer<FakeBackend>> made;
    int calls = 0;

    void install()
    {
        FilePicker::setBackendFactoryForTests([this](FilePicker::Mode) {
            ++calls;
            std::vector<std::unique_ptr<FilePickerBackend>> out;
            for (const bool can : std::as_const(canShow)) {
                auto fake = std::make_unique<FakeBackend>(can);
                made.append(fake.get());
                out.push_back(std::move(fake));
            }
            return out;
        });
    }
    FakeBackend *last() const { return made.isEmpty() ? nullptr : made.last().data(); }
};

QUrl local(const QString &path)
{
    return QUrl::fromLocalFile(path);
}

} // namespace

class TestFilePicker : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { FilePicker::setBackendFactoryForTests({}); }

    void withDefaultSuffix_data()
    {
        QTest::addColumn<QUrl>("file");
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QUrl>("expected");
        QTest::newRow("no dot") << local(u"/home/ja/Notatka"_s) << u"md"_s << local(u"/home/ja/Notatka.md"_s);
        QTest::newRow("Polish letters, spaces")
                << local(u"/home/ja/Zażółć gęślą jaźń"_s) << u"md"_s << local(u"/home/ja/Zażółć gęślą jaźń.md"_s);
        QTest::newRow("percent sign kept")
                << local(u"/home/ja/50% planu"_s) << u"html"_s << local(u"/home/ja/50% planu.html"_s);
        QTest::newRow("encoded-looking name") << local(u"/home/ja/a%41b"_s) << u"md"_s << local(u"/home/ja/a%41b.md"_s);
        QTest::newRow("has a dot") << local(u"/home/ja/Notatka.v2"_s) << u"md"_s << local(u"/home/ja/Notatka.v2"_s);
        QTest::newRow("has the suffix") << local(u"/home/ja/a.md"_s) << u"md"_s << local(u"/home/ja/a.md"_s);
        QTest::newRow("dot in a folder only")
                << local(u"/home/ja.kowalski/Notatka"_s) << u"md"_s << local(u"/home/ja.kowalski/Notatka.md"_s);
        QTest::newRow("suffix with a dot") << local(u"/tmp/ćma"_s) << u".oga"_s << local(u"/tmp/ćma.oga"_s);
        QTest::newRow("content document")
                << QUrl(u"content://com.android.providers.downloads.documents/document/42"_s) << u"md"_s
                << QUrl(u"content://com.android.providers.downloads.documents/document/42"_s);
        QTest::newRow("folder") << QUrl(u"file:///tmp/dir/"_s) << u"md"_s << QUrl(u"file:///tmp/dir/"_s);
        QTest::newRow("empty suffix") << local(u"/tmp/Notatka"_s) << QString() << local(u"/tmp/Notatka"_s);
        QTest::newRow("dots only") << local(u"/tmp/Notatka"_s) << u".."_s << local(u"/tmp/Notatka"_s);
        QTest::newRow("empty url") << QUrl() << u"md"_s << QUrl();
    }
    void withDefaultSuffix()
    {
        QFETCH(QUrl, file);
        QFETCH(QString, suffix);
        QFETCH(QUrl, expected);
        const QUrl got = FilePicker::withDefaultSuffix(file, suffix);
        QCOMPARE(got, expected);
        if (expected.isLocalFile())
            QCOMPARE(got.toLocalFile(), expected.toLocalFile());
    }

    void cleanNameFilters_data()
    {
        QTest::addColumn<QStringList>("filters");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("none") << QStringList() << QStringList();
        QTest::newRow("one empty") << QStringList{QString()} << QStringList();
        QTest::newRow("blank") << QStringList{u"  "_s} << QStringList();
        QTest::newRow("filter and empty")
                << QStringList{u"Obrazy (*.png *.jpg)"_s, QString()} << QStringList{u"Obrazy (*.png *.jpg)"_s};
        QTest::newRow("malformed kept") << QStringList{u"Obrazy (*.png"_s} << QStringList{u"Obrazy (*.png"_s};
        QTest::newRow("star") << QStringList{u"*"_s} << QStringList{u"*"_s};
        QTest::newRow("all files") << QStringList{u"Wszystkie pliki (*)"_s} << QStringList{u"Wszystkie pliki (*)"_s};
    }
    void cleanNameFilters()
    {
        QFETCH(QStringList, filters);
        QFETCH(QStringList, expected);
        QCOMPARE(FilePicker::cleanNameFilters(filters), expected);
    }

    void modesMapToRequest_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("OpenFile") << int(FilePicker::OpenFile);
        QTest::newRow("OpenFiles") << int(FilePicker::OpenFiles);
        QTest::newRow("OpenFolder") << int(FilePicker::OpenFolder);
        QTest::newRow("SaveFile") << int(FilePicker::SaveFile);
    }
    void modesMapToRequest()
    {
        QFETCH(int, mode);
        Fakes fakes;
        fakes.install();
        FilePicker picker;
        picker.setFileMode(FilePicker::Mode(mode));
        picker.setTitle(u"Zapisz — zażółć gęślą jaźń"_s);
        picker.setNameFilters({u"Notatki (*.md *.txt)"_s, QString(), u"Wszystkie pliki (*)"_s});
        picker.setDefaultSuffix(u"md"_s);
        picker.setCurrentFolder(local(u"/home/ja/Dokumenty"_s));
        picker.setSelectedFile(local(u"/home/ja/Dokumenty/Źródła.md"_s));
        picker.setDark(true);
        picker.open();
        QVERIFY(fakes.last());
        const FilePickerRequest r = fakes.last()->lastRequest;
        QCOMPARE(int(r.mode), mode);
        QCOMPARE(r.title, u"Zapisz — zażółć gęślą jaźń"_s);
        QCOMPARE(r.nameFilters, (QStringList{u"Notatki (*.md *.txt)"_s, u"Wszystkie pliki (*)"_s}));
        QCOMPARE(r.defaultSuffix, u"md"_s);
        QCOMPARE(r.folder, local(u"/home/ja/Dokumenty"_s));
        // The suggested file only for saving (Android's EXTRA_TITLE).
        QCOMPARE(r.initialFile, mode == FilePicker::SaveFile ? local(u"/home/ja/Dokumenty/Źródła.md"_s) : QUrl());
        QVERIFY(r.dark);
    }

    void acceptAndReject_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("OpenFiles") << int(FilePicker::OpenFiles);
        QTest::newRow("OpenFolder") << int(FilePicker::OpenFolder);
    }
    void acceptAndReject()
    {
        QFETCH(int, mode);
        Fakes fakes;
        fakes.install();
        FilePicker picker;
        picker.setFileMode(FilePicker::Mode(mode));
        QStringList log;
        connect(&picker, &FilePicker::selectedFilesChanged, this, [&] { log << u"selected"_s; });
        connect(&picker, &FilePicker::visibleChanged, this,
                [&] { log << (picker.isVisible() ? u"visible"_s : u"hidden"_s); });
        connect(&picker, &FilePicker::accepted, this, [&] { log << u"accepted"_s; });
        connect(&picker, &FilePicker::rejected, this, [&] { log << u"rejected"_s; });

        picker.open();
        QVERIFY(picker.isVisible());
        QCOMPARE(log, QStringList{u"visible"_s});
        const QList<QUrl> picked{local(u"/home/ja/Ćma.oga"_s), QUrl(u"content://x/tree/Źródła"_s)};
        Q_EMIT fakes.last()->accepted(picked);
        QCOMPARE(log, (QStringList{u"visible"_s, u"selected"_s, u"hidden"_s, u"accepted"_s}));
        QVERIFY(!picker.isVisible());
        QCOMPARE(picker.selectedFiles(), picked);
        QCOMPARE(picker.selectedFile(), picked.first());
        QCOMPARE(picker.property("selectedFolder").toUrl(), picked.first());
        QCOMPARE(fakes.last()->hidden, 1); // closed and deleted later

        // Cancelled: the selection stays.
        log.clear();
        picker.open();
        Q_EMIT fakes.last()->rejected();
        QCOMPARE(log, (QStringList{u"visible"_s, u"hidden"_s, u"rejected"_s}));
        QCOMPARE(picker.selectedFiles(), picked);

        // Accepted with nothing chosen (some portals): cancelled.
        log.clear();
        picker.open();
        Q_EMIT fakes.last()->accepted({QUrl()});
        QCOMPARE(log, (QStringList{u"visible"_s, u"hidden"_s, u"rejected"_s}));
        QCOMPARE(picker.selectedFiles(), picked);
        QCOMPARE(fakes.calls, 3);
    }

    void saveAddsSuffix()
    {
        Fakes fakes;
        fakes.install();
        FilePicker picker;
        picker.setFileMode(FilePicker::SaveFile);
        picker.setDefaultSuffix(u"md"_s);
        picker.open();
        Q_EMIT fakes.last()->accepted({local(u"/home/ja/Zażółć gęślą jaźń"_s)});
        QCOMPARE(picker.selectedFile(), local(u"/home/ja/Zażółć gęślą jaźń.md"_s));
        picker.open();
        const QUrl document(u"content://com.android.externalstorage.documents/document/primary%3ADownload%2Fa"_s);
        Q_EMIT fakes.last()->accepted({document});
        QCOMPARE(picker.selectedFile(), document);
        // Not for opening.
        picker.setFileMode(FilePicker::OpenFile);
        picker.open();
        Q_EMIT fakes.last()->accepted({local(u"/home/ja/Notatka"_s)});
        QCOMPARE(picker.selectedFile(), local(u"/home/ja/Notatka"_s));
    }

    void rejectClosesThePicker()
    {
        Fakes fakes;
        fakes.install();
        FilePicker picker;
        QSignalSpy rejected(&picker, &FilePicker::rejected);
        picker.reject(); // not open: nothing
        QCOMPARE(rejected.size(), 0);
        picker.open();
        const QPointer<FakeBackend> fake = fakes.last();
        picker.reject();
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!picker.isVisible());
        QVERIFY(fake);
        QCOMPARE(fake->hidden, 1);
        // A late answer of the closed picker goes nowhere.
        QSignalSpy accepted(&picker, &FilePicker::accepted);
        Q_EMIT fake->accepted({local(u"/tmp/a.md"_s)});
        QCOMPARE(accepted.size(), 0);
        QVERIFY(picker.selectedFiles().isEmpty());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!fake);
    }

    void openWhileOpenIsIgnored()
    {
        Fakes fakes;
        fakes.install();
        FilePicker picker;
        QSignalSpy visible(&picker, &FilePicker::visibleChanged);
        picker.open();
        picker.open();
        QCOMPARE(fakes.calls, 1);
        QCOMPARE(fakes.last()->shown, 1);
        QCOMPARE(visible.size(), 1);
    }

    void destroyWhileOpenHidesTheBackend()
    {
        Fakes fakes;
        fakes.install();
        auto *picker = new FilePicker;
        bool signalled = false;
        connect(picker, &FilePicker::accepted, this, [&] { signalled = true; });
        connect(picker, &FilePicker::rejected, this, [&] { signalled = true; });
        picker->open();
        const QPointer<FakeBackend> fake = fakes.last();
        delete picker;
        QVERIFY(fake);
        QCOMPARE(fake->hidden, 1);
        // Android: a result already on its way reaches nobody.
        Q_EMIT fake->accepted({local(u"/tmp/a.md"_s)});
        Q_EMIT fake->rejected();
        QVERIFY(!signalled);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!fake);
    }

    void firstBackendThatShowsWins()
    {
        Fakes fakes;
        fakes.canShow = {false, true, true};
        fakes.install();
        FilePicker picker;
        picker.setTitle(u"Wstaw obraz"_s);
        picker.open();
        QVERIFY(picker.isVisible());
        QCOMPARE(fakes.made.size(), 3);
        QVERIFY(!fakes.made.at(0)); // could not show: deleted at once
        QVERIFY(fakes.made.at(1));
        QCOMPARE(fakes.made.at(1)->shown, 1);
        QCOMPARE(fakes.made.at(1)->lastRequest.title, u"Wstaw obraz"_s);
        QVERIFY(!fakes.made.at(2)); // never asked
        Q_EMIT fakes.made.at(1)->accepted({local(u"/tmp/żaba.png"_s)});
        QCOMPARE(picker.selectedFile(), local(u"/tmp/żaba.png"_s));
    }

    void noBackendRejects()
    {
        Fakes fakes;
        fakes.canShow = {false};
        fakes.install();
        FilePicker picker;
        QSignalSpy rejected(&picker, &FilePicker::rejected);
        QSignalSpy visible(&picker, &FilePicker::visibleChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No file picker could be shown"_s));
        picker.open();
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(visible.size(), 0);
        QVERIFY(!picker.isVisible());

        fakes.canShow = {};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No file picker could be shown"_s));
        picker.open();
        QCOMPARE(rejected.size(), 2);
    }

    void parentWindowFromItem()
    {
        Fakes fakes;
        fakes.install();
        QQuickWindow window;
        auto *item = new QQuickItem(window.contentItem());
        auto *holder = new QObject(item); // a QML object inside an Item: a QObject child of it
        FilePicker picker(holder);
        QCOMPARE(picker.parentWindow(), &window);
        picker.open();
        QCOMPARE(fakes.last()->lastParent, &window);
        picker.reject();

        QQuickWindow other;
        picker.setParentWindow(&other);
        QCOMPARE(picker.parentWindow(), &other);
        picker.setParentWindow(nullptr); // back to the item's window
        QCOMPARE(picker.parentWindow(), &window);

        FilePicker alone;
        QCOMPARE(alone.parentWindow(), nullptr);
    }

    void qmlApi()
    {
        Fakes fakes;
        fakes.install();
        qmlRegisterType<FilePicker>("BrulionTest", 1, 0, "FilePicker");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(import BrulionTest
FilePicker {
    property int acceptedCount: 0
    property url first
    fileMode: FilePicker.SaveFile
    title: "Zapisz stronę jako"
    nameFilters: ["Pliki Markdown (*.md)"]
    defaultSuffix: "md"
    dark: true
    onAccepted: { acceptedCount++; first = selectedFiles[0] }
})qml",
                          QUrl(u"qrc:/tst_filepicker.qml"_s));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *picker = qobject_cast<FilePicker *>(object.get());
        QVERIFY(picker);
        QCOMPARE(picker->fileMode(), FilePicker::SaveFile);
        QVERIFY(picker->available());
        QMetaObject::invokeMethod(picker, "open");
        QVERIFY(picker->property("visible").toBool());
        QCOMPARE(fakes.last()->lastRequest.title, u"Zapisz stronę jako"_s);
        Q_EMIT fakes.last()->accepted({local(u"/tmp/Źródła"_s)});
        QCOMPARE(object->property("acceptedCount").toInt(), 1);
        QCOMPARE(object->property("first").toUrl(), local(u"/tmp/Źródła.md"_s));
        QCOMPARE(object->property("selectedFile").toUrl(), local(u"/tmp/Źródła.md"_s));
    }

#ifdef BRULION_FILEPICKER_WIDGETS
    /// Offscreen there is no native dialog: Qt Widgets' QFileDialog, shown and driven for real.
    void widgetFallbackOffscreen()
    {
        QVERIFY(qobject_cast<QApplication *>(QCoreApplication::instance()));
        QVERIFY(!brulion::ui::createNativeFilePicker());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(u"Ćma — zażółć.oga"_s);
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("OggS");
        }

        FilePicker picker;
        picker.setFileMode(FilePicker::OpenFile);
        picker.setTitle(u"Wybierz plik dźwiękowy"_s);
        picker.setNameFilters({u"Pliki dźwiękowe (*.oga *.ogg *.wav)"_s, u"Wszystkie pliki (*)"_s});
        picker.setCurrentFolder(QUrl::fromLocalFile(dir.path()));
        picker.setDark(true);
        QSignalSpy accepted(&picker, &FilePicker::accepted);
        picker.open();
        QVERIFY(picker.isVisible());
        QFileDialog *dialog = shownFileDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), u"Wybierz plik dźwiękowy"_s);
        QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFile);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptOpen);
        QVERIFY(dialog->testOption(QFileDialog::DontUseNativeDialog));
        QCOMPARE(dialog->nameFilters().size(), 2);
        QCOMPARE(dialog->directory().canonicalPath(), QDir(dir.path()).canonicalPath());
        QCOMPARE(dialog->palette().color(QPalette::Window), QColor(0x2b, 0x2b, 0x2b));
        dialog->selectFile(file);
        static_cast<QDialog *>(dialog)->accept(); // QFileDialog::accept() is protected
        QCOMPARE(accepted.size(), 1);
        QVERIFY(!picker.isVisible());
        QCOMPARE(QFileInfo(picker.selectedFile().toLocalFile()).canonicalFilePath(),
                 QFileInfo(file).canonicalFilePath());

        // Saving: the default suffix; cancelled with reject().
        picker.setFileMode(FilePicker::SaveFile);
        picker.setDefaultSuffix(u"md"_s);
        picker.setDark(false);
        picker.setSelectedFile(QUrl::fromLocalFile(dir.filePath(u"Notatka"_s)));
        picker.open();
        dialog = shownFileDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptSave);
        QVERIFY(dialog->palette().color(QPalette::Window) != QColor(0x2b, 0x2b, 0x2b));
        static_cast<QDialog *>(dialog)->accept(); // QFileDialog::accept() is protected
        QCOMPARE(accepted.size(), 2);
        QCOMPARE(picker.selectedFile().fileName(), u"Notatka.md"_s);

        QSignalSpy rejected(&picker, &FilePicker::rejected);
        picker.setFileMode(FilePicker::OpenFolder);
        picker.open();
        dialog = shownFileDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->fileMode(), QFileDialog::Directory);
        const QPointer<QFileDialog> closing = dialog;
        picker.reject();
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!picker.isVisible());
        QVERIFY(!closing || !closing->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!closing);

        // The user's cancel in the dialog itself.
        picker.open();
        dialog = shownFileDialog();
        QVERIFY(dialog);
        dialog->reject();
        QCOMPARE(rejected.size(), 2);
        QVERIFY(!picker.isVisible());
    }

private:
    static QFileDialog *shownFileDialog()
    {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *d = qobject_cast<QFileDialog *>(w); d && d->isVisible())
                return d;
        }
        return nullptr;
    }
#endif
};

int main(int argc, char **argv)
{
    // Offscreen and without a platform theme of the developer's desktop (gtk3, a portal): no
    // native dialog, so the widget fallback is what the test sees.
    qunsetenv("QT_QPA_PLATFORMTHEME");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef BRULION_FILEPICKER_WIDGETS
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
    TestFilePicker test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_filepicker.moc"
