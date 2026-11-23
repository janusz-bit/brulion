// The note's own options in the block editor (docs/UI.md "Note options", docs/SYNTAX.md §4 "Note
// options"): PageController.noteOptions and its typed properties following the text (also source
// mode), setNoteOption writing the interface's key or the note's own, one undo step each, "As in
// Settings" removing every spelling, the rest of the file byte for byte, refusals.
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Kod.md"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content, const QString &locale = u"pl"_s)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-10-28 10:00"_s);
        page.setSaveDelay(0);
        page.setLocale(locale);
        page.setHost(&host);
        page.setPath(Page);
    }
    QString option(const QString &name) const { return page.noteOptions().value(name).toString(); }
    bool set(const QString &name, const QString &value)
    {
        return page.setNoteOption(name, value).value(u"ok"_s).toBool();
    }
};

} // namespace

class TestEditorNoteOptions : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void readsTheOptions()
    {
        Fixture f(u"---\nnumery-linii: true\nramki: nie\nszerokosc-tekstu: szeroka\nzadania: ignoruj\n---\n- Zażółć gęślą jaźń\n"_s);
        QCOMPARE(f.option(u"lineNumbers"_s), u"on"_s);
        QCOMPARE(f.option(u"pictureFrames"_s), u"off"_s);
        QCOMPARE(f.option(u"textWidth"_s), u"wide"_s);
        QCOMPARE(f.option(u"tasks"_s), u"ignore"_s);
        QCOMPARE(f.page.codeLineNumbers(), 1);
        QCOMPARE(f.page.pictureFrames(), 0);
        QCOMPARE(f.page.textWidth(), 960);
    }

    void noOptions()
    {
        Fixture f(u"# Notatka\n\nTekst\n"_s);
        QCOMPARE(f.option(u"lineNumbers"_s), QString());
        QCOMPARE(f.option(u"textWidth"_s), QString());
        QCOMPARE(f.option(u"tasks"_s), QString());
        QCOMPARE(f.page.codeLineNumbers(), -1);
        QCOMPARE(f.page.pictureFrames(), -1);
        QCOMPARE(f.page.textWidth(), -1);
    }

    void setWritesTheInterfacesKey()
    {
        const QString before = u"---\ntags: [kod]\n---\n- Zażółć gęślą jaźń\r\n"_s;
        Fixture f(before);
        QSignalSpy changed(&f.page, &PageController::noteOptionsChanged);
        QVERIFY(f.set(u"lineNumbers"_s, u"on"_s));
        QCOMPARE(f.page.text(), u"---\ntags: [kod]\nnumery-linii: true\n---\n- Zażółć gęślą jaźń\r\n"_s);
        QCOMPARE(f.page.codeLineNumbers(), 1);
        QCOMPARE(changed.count(), 1);
        QVERIFY(f.set(u"textWidth"_s, u"full"_s));
        QVERIFY(f.page.text().contains(u"\nszerokosc-tekstu: pełna\n"_s));
        QCOMPARE(f.page.textWidth(), 0);
        QVERIFY(f.set(u"tasks"_s, u"ignore"_s));
        QVERIFY(f.page.text().contains(u"\nzadania: ignoruj\n"_s));
        QCOMPARE(f.option(u"tasks"_s), u"ignore"_s);
        // Changed in place: one line.
        QVERIFY(f.set(u"lineNumbers"_s, u"off"_s));
        QVERIFY(f.page.text().contains(u"\nnumery-linii: false\n"_s));
        QCOMPARE(f.page.codeLineNumbers(), 0);
        // "As in Settings" removes them: the text as it was.
        QVERIFY(f.set(u"lineNumbers"_s, QString()));
        QVERIFY(f.set(u"textWidth"_s, QString()));
        QVERIFY(f.set(u"tasks"_s, QString()));
        QCOMPARE(f.page.text(), before);
        QCOMPARE(f.page.codeLineNumbers(), -1);
        // Each edit was one undo step.
        f.page.undo();
        QVERIFY(f.page.text().contains(u"\nzadania: ignoruj\n"_s));
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.page.codeLineNumbers(), 0);
        f.page.undo();
        QCOMPARE(f.page.codeLineNumbers(), 1);
    }

    void englishInterface()
    {
        Fixture f(u"Tekst\n"_s, u"en"_s);
        QVERIFY(f.set(u"pictureFrames"_s, u"on"_s));
        QVERIFY(f.set(u"textWidth"_s, u"narrow"_s));
        QVERIFY(f.set(u"tasks"_s, u"ignore"_s));
        QCOMPARE(f.page.text(), u"---\npicture-frames: true\ntext-width: narrow\ntasks: ignore\n---\nTekst\n"_s);
        QCOMPARE(f.page.pictureFrames(), 1);
        QCOMPARE(f.page.textWidth(), 600);
    }

    /// The note's key keeps its place, its spelling and its language, whatever the interface's.
    void theNotesKeyStays()
    {
        Fixture f(u"---\nSzerokosc-Tekstu: wąska\nZadania: nie\n---\nTekst\n"_s, u"en"_s);
        QCOMPARE(f.page.textWidth(), 600);
        QVERIFY(f.set(u"textWidth"_s, u"wide"_s));
        QVERIFY(f.set(u"tasks"_s, u"ignore"_s));
        QCOMPARE(f.page.text(), u"---\nSzerokosc-Tekstu: szeroka\nZadania: ignoruj\n---\nTekst\n"_s);
        Fixture g(u"---\nText-Width: full\n---\nTekst\n"_s);
        QVERIFY(g.set(u"textWidth"_s, u"normal"_s));
        QCOMPARE(g.page.text(), u"---\nText-Width: normal\n---\nTekst\n"_s);
    }

    /// "As in Settings" takes out every spelling of the option, in both blocks.
    void removingTakesEverySpelling()
    {
        Fixture f(u"---\nline-numbers: true\nnumery-linii: false\na: 1\n---\nTekst\n"_s);
        QCOMPARE(f.page.codeLineNumbers(), 1);
        QVERIFY(f.set(u"lineNumbers"_s, QString()));
        QCOMPARE(f.page.text(), u"---\na: 1\n---\nTekst\n"_s);
        Fixture g(u"tasks:: ignore\nzadania:: ignoruj\nalias:: Kod\n\n- blok\n"_s);
        QVERIFY(g.set(u"tasks"_s, QString()));
        QCOMPARE(g.page.text(), u"alias:: Kod\n\n- blok\n"_s);
        // Nothing to remove: unchanged, still ok.
        const QVariantMap r = g.page.setNoteOption(u"lineNumbers"_s, QString());
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"status"_s).toString(), u"unchanged"_s);
    }

    void logseqPage()
    {
        Fixture f(u"alias:: Kod\n\n- blok\n"_s);
        QVERIFY(f.set(u"lineNumbers"_s, u"on"_s));
        QCOMPARE(f.page.text(), u"alias:: Kod\nnumery-linii:: true\n\n- blok\n"_s);
        QCOMPARE(f.page.codeLineNumbers(), 1);
    }

    void sourceModeFollows()
    {
        Fixture f(u"Tekst\n"_s);
        f.page.setSourceMode(true);
        QVERIFY(f.set(u"pictureFrames"_s, u"off"_s));
        QCOMPARE(f.page.sourceText(), u"---\nramki: false\n---\nTekst\n"_s);
        QCOMPARE(f.page.pictureFrames(), 0);
        // Typed in the source: the options follow when the source is left.
        f.page.setSourceText(u"---\nramki: tak\n---\nTekst\n"_s);
        f.page.setSourceMode(false);
        QCOMPARE(f.page.pictureFrames(), 1);
    }

    void refusals()
    {
        const QString text = u"---\na: 1\n---\nTekst\n"_s;
        Fixture f(text);
        for (const auto &[name, value] : {std::pair{u"lineNumbers"_s, u"tak"_s},
                                          std::pair{u"textWidth"_s, u"wąska"_s},
                                          std::pair{u"textWidth"_s, u"960"_s},
                                          std::pair{u"tasks"_s, u"on"_s},
                                          std::pair{u"line-numbers"_s, u"on"_s},
                                          std::pair{QString(), QString()}}) {
            const QVariantMap r = f.page.setNoteOption(name, value);
            QVERIFY2(!r.value(u"ok"_s).toBool(), qPrintable(name + u' ' + value));
            QCOMPARE(r.value(u"status"_s).toString(), u"invalidValue"_s);
            QVERIFY(!r.value(u"reason"_s).toString().isEmpty());
        }
        QCOMPARE(f.page.text(), text);
        // A frontmatter that cannot be edited safely is left alone.
        Fixture bad(u"---\n- lista\n---\nTekst\n"_s);
        QVERIFY(!bad.set(u"lineNumbers"_s, u"on"_s));
        QCOMPARE(bad.page.text(), u"---\n- lista\n---\nTekst\n"_s);
        // Reading mode: nothing is written.
        Fixture reading(text);
        reading.page.setReadOnly(true);
        QVERIFY(!reading.set(u"lineNumbers"_s, u"on"_s));
        QCOMPARE(reading.page.text(), text);
    }
};

QTEST_MAIN(TestEditorNoteOptions)
#include "tst_editor_noteoptions.moc"
