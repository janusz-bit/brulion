// A note's own options (markdown/noteoptions.h, docs/SYNTAX.md §4 "Note options"): the flags and
// widths read from YAML frontmatter and Logseq lines, the English and Polish keys, which key
// decides, what is no option, and the values Brulion writes reading back as written.
#include "markdown/noteoptions.h"
#include "markdown/pageprops.h"
#include "markdown/parse.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

NoteOptions optionsOf(const QString &content)
{
    return noteOptions(parseNote(u"Notatka.md"_s, content, VaultConfig()).properties);
}

/// -1 none, 0 false, 1 true.
int flag(std::optional<bool> value)
{
    return value ? int(*value) : -1;
}

/// -1 none, else the NoteTextWidth.
int width(std::optional<NoteTextWidth> value)
{
    return value ? int(*value) : -1;
}

} // namespace

class TestNoteOptions : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void flags_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<int>("expected");
        QTest::newRow("bool true") << QVariant(true) << 1;
        QTest::newRow("bool false") << QVariant(false) << 0;
        QTest::newRow("1") << QVariant(qlonglong(1)) << 1;
        QTest::newRow("0") << QVariant(qlonglong(0)) << 0;
        QTest::newRow("2") << QVariant(qlonglong(2)) << -1;
        QTest::newRow("0.5") << QVariant(0.5) << -1;
        QTest::newRow("tak") << QVariant(u"tak"_s) << 1;
        QTest::newRow("NIE") << QVariant(u"NIE"_s) << 0;
        QTest::newRow("Yes") << QVariant(u"Yes"_s) << 1;
        QTest::newRow("off") << QVariant(u" off "_s) << 0;
        QTest::newRow("on") << QVariant(u"on"_s) << 1;
        QTest::newRow("quoted") << QVariant(u"\"true\""_s) << 1;
        QTest::newRow("list") << QVariant(QStringList{u"false"_s, u"true"_s}) << 0;
        QTest::newRow("empty") << QVariant(u""_s) << -1;
        QTest::newRow("invalid") << QVariant() << -1;
        QTest::newRow("word") << QVariant(u"może"_s) << -1;
        QTest::newRow("prawda") << QVariant(u"prawda"_s) << -1;
    }
    void flags()
    {
        QFETCH(QVariant, value);
        QFETCH(int, expected);
        QCOMPARE(flag(parseNoteFlag(value)), expected);
    }

    void widths_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("expected");
        QTest::newRow("narrow") << u"narrow"_s << int(NoteTextWidth::Narrow);
        QTest::newRow("Wide") << u"Wide"_s << int(NoteTextWidth::Wide);
        QTest::newRow("FULL") << u"FULL"_s << int(NoteTextWidth::Full);
        QTest::newRow("normal") << u"normal"_s << int(NoteTextWidth::Normal);
        QTest::newRow("wąska") << u"wąska"_s << int(NoteTextWidth::Narrow);
        QTest::newRow("waska") << u"waska"_s << int(NoteTextWidth::Narrow);
        QTest::newRow("WĄSKA") << u"WĄSKA"_s << int(NoteTextWidth::Narrow);
        QTest::newRow("pełna") << u"pełna"_s << int(NoteTextWidth::Full);
        QTest::newRow("pelna") << u"pelna"_s << int(NoteTextWidth::Full);
        QTest::newRow("szeroka") << u"szeroka"_s << int(NoteTextWidth::Wide);
        QTest::newRow("normalna") << u"normalna"_s << int(NoteTextWidth::Normal);
        QTest::newRow("pixels") << u"960"_s << -1;
        QTest::newRow("wider") << u"wider"_s << -1;
        QTest::newRow("empty") << u""_s << -1;
    }
    void widths()
    {
        QFETCH(QString, value);
        QFETCH(int, expected);
        QCOMPARE(width(parseNoteTextWidth(value)), expected);
    }

    void widthNamesAndPixels()
    {
        for (const NoteTextWidth w :
             {NoteTextWidth::Narrow, NoteTextWidth::Normal, NoteTextWidth::Wide, NoteTextWidth::Full}) {
            QCOMPARE(noteTextWidthFromName(noteTextWidthName(w)), w);
            QCOMPARE(parseNoteTextWidth(noteTextWidthValue(w, true)), w);
            QCOMPARE(parseNoteTextWidth(noteTextWidthValue(w, false)), w);
        }
        QCOMPARE(noteTextWidthValue(NoteTextWidth::Narrow, true), u"wąska"_s);
        QCOMPARE(noteTextWidthPixels(NoteTextWidth::Narrow), 600);
        QCOMPARE(noteTextWidthPixels(NoteTextWidth::Normal), 680);
        QCOMPARE(noteTextWidthPixels(NoteTextWidth::Wide), 960);
        QCOMPARE(noteTextWidthPixels(NoteTextWidth::Full), 0);
        QCOMPARE(noteTextWidthFromName(u"pełna"), std::nullopt);
    }

    void names()
    {
        for (const NoteOption o :
             {NoteOption::LineNumbers, NoteOption::PictureFrames, NoteOption::TextWidth, NoteOption::Tasks,
              NoteOption::PictureBacking})
            QCOMPARE(parseNoteOptionName(noteOptionName(o)), o);
        QCOMPARE(parseNoteOptionName(u"pictureBacking"), NoteOption::PictureBacking);
        QCOMPARE(parseNoteOptionName(u"lineNumbers"), NoteOption::LineNumbers);
        QCOMPARE(parseNoteOptionName(u"line-numbers"), std::nullopt);
        QCOMPARE(noteOptionKey(NoteOption::LineNumbers, false), u"line-numbers"_s);
        QCOMPARE(noteOptionKey(NoteOption::LineNumbers, true), u"numery-linii"_s);
        QCOMPARE(noteOptionKey(NoteOption::PictureFrames, true), u"ramki"_s);
        QCOMPARE(noteOptionKey(NoteOption::TextWidth, true), u"szerokosc-tekstu"_s);
        QCOMPARE(noteOptionKey(NoteOption::Tasks, true), u"zadania"_s);
        QCOMPARE(noteOptionKey(NoteOption::PictureBacking, false), u"picture-backing"_s);
        QCOMPARE(noteOptionKey(NoteOption::PictureBacking, true), u"jasne-tlo"_s);
    }

    /// The light backing under pictures in a dark theme (`picture-backing` / `jasne-tlo`): a flag.
    void pictureBacking_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("backing");
        const int none = -1;
        QTest::newRow("nothing") << u"# Notatka\n\n![[stos.png]]\n"_s << none;
        QTest::newRow("yaml off") << u"---\npicture-backing: false\n---\n![[stos.png]]\n"_s << 0;
        QTest::newRow("yaml polish on") << u"---\njasne-tlo: tak\n---\nZażółć gęślą jaźń\n"_s << 1;
        QTest::newRow("any case") << u"---\nJasne-Tlo: NIE\n---\nTekst\n"_s << 0;
        QTest::newRow("logseq crlf") << u"jasne-tlo:: on\r\n\r\n- blok\r\n"_s << 1;
        QTest::newRow("english first") << u"---\njasne-tlo: true\npicture-backing: false\n---\n"_s << 0;
        QTest::newRow("no value") << u"---\npicture-backing: może\n---\n"_s << none;
        QTest::newRow("block property") << u"- blok\n  picture-backing:: false\n"_s << none;
    }
    void pictureBacking()
    {
        QFETCH(QString, content);
        QFETCH(int, backing);
        const NoteOptions o = optionsOf(content);
        QCOMPARE(flag(o.pictureBacking), backing);
        // (The other options are left alone.)
        QCOMPARE(flag(o.pictureFrames), -1);
    }

    void fromNotes_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("lineNumbers");
        QTest::addColumn<int>("pictureFrames");
        QTest::addColumn<int>("textWidth");
        QTest::addColumn<bool>("tasksIgnored");
        const int none = -1;
        QTest::newRow("nothing") << u"# Notatka\n\nTekst."_s << none << none << none << false;
        QTest::newRow("yaml") << u"---\nline-numbers: true\npicture-frames: false\ntext-width: wide\n---\n# Kod\n"_s
                              << 1 << 0 << int(NoteTextWidth::Wide) << false;
        QTest::newRow("yaml polish") << u"---\nnumery-linii: tak\nramki: nie\nszerokosc-tekstu: pełna\nzadania: ignoruj\n---\nTekst"_s
                                     << 1 << 0 << int(NoteTextWidth::Full) << true;
        QTest::newRow("yaml keys any case") << u"---\nLine-Numbers: false\nRAMKI: true\n---\nTekst"_s << 0 << 1 << none
                                            << false;
        QTest::newRow("logseq") << u"numery-linii:: true\nszerokosc-tekstu:: wąska\ntasks:: ignore\n\n- blok"_s << 1 << none
                                << int(NoteTextWidth::Narrow) << true;
        QTest::newRow("logseq crlf") << u"ramki:: false\r\n\r\n- zażółć gęślą jaźń\r\n"_s << none << 0 << none << false;
        // The English key decides over the Polish one.
        QTest::newRow("english first") << u"---\nnumery-linii: true\nline-numbers: false\n---\n"_s << 0 << none << none
                                       << false;
        // A value that is none of the option's is no option: the device's setting holds.
        QTest::newRow("no values") << u"---\nline-numbers: może\nramki: 3\ntext-width: 960\n---\n"_s << none << none
                                   << none << false;
        QTest::newRow("empty values") << u"---\nline-numbers:\ntext-width: \"\"\n---\n"_s << none << none << none
                                      << false;
        // Block properties are no note options.
        QTest::newRow("block property") << u"# Notatka\n\n- blok\n  line-numbers:: true\n"_s << none << none << none
                                        << false;
        // Not at the very top: the frontmatter is the page's text.
        QTest::newRow("late frontmatter") << u"Tekst\n\n---\nline-numbers: true\n---\n"_s << none << none << none
                                          << false;
        QTest::newRow("malformed yaml") << u"---\nline-numbers: [true\n---\nTekst"_s << none << none << none << false;
    }
    void fromNotes()
    {
        QFETCH(QString, content);
        QFETCH(int, lineNumbers);
        QFETCH(int, pictureFrames);
        QFETCH(int, textWidth);
        QFETCH(bool, tasksIgnored);
        const NoteOptions o = optionsOf(content);
        QCOMPARE(flag(o.lineNumbers), lineNumbers);
        QCOMPARE(flag(o.pictureFrames), pictureFrames);
        QCOMPARE(width(o.textWidth), textWidth);
        QCOMPARE(o.tasksIgnored, tasksIgnored);
    }

    /// What Brulion writes (a checkbox `true`/`false`, a width's name, `ignoruj`) reads back as
    /// that option, in a frontmatter and in Logseq lines, and only the option's line changes.
    void writtenValuesReadBack_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("polish");
        QTest::newRow("no properties") << u"# Notatka\n\nZażółć gęślą jaźń.\n"_s << true;
        QTest::newRow("yaml") << u"---\ntags: [kod]\n---\n# Notatka\n"_s << false;
        QTest::newRow("yaml crlf") << u"---\r\ntags: [kod]\r\n---\r\nTekst\r\n"_s << true;
        QTest::newRow("logseq") << u"alias:: Kod\n\n- blok\n"_s << true;
    }
    void writtenValuesReadBack()
    {
        QFETCH(QString, content);
        QFETCH(bool, polish);
        QString text = content;
        const auto set = [&](NoteOption option, const QVariant &value, PropertyType type) {
            const PageProperties props = readPageProperties(text);
            const PropertyEditResult r =
                    setPageProperty(text, props.addTarget(), noteOptionKey(option, polish), value, type);
            QCOMPARE(r.status, PropertyEditStatus::Changed);
            text = r.content;
        };
        set(NoteOption::LineNumbers, true, PropertyType::Checkbox);
        set(NoteOption::PictureFrames, false, PropertyType::Checkbox);
        set(NoteOption::PictureBacking, false, PropertyType::Checkbox);
        set(NoteOption::TextWidth, noteTextWidthValue(NoteTextWidth::Full, polish), PropertyType::Text);
        set(NoteOption::Tasks, polish ? u"ignoruj"_s : u"ignore"_s, PropertyType::Text);
        const NoteOptions o = optionsOf(text);
        QCOMPARE(o.lineNumbers, true);
        QCOMPARE(o.pictureFrames, false);
        QCOMPARE(o.pictureBacking, false);
        QCOMPARE(o.textWidth, NoteTextWidth::Full);
        QVERIFY(o.tasksIgnored);
        // The text after the properties stays as it was.
        const QString body = content.mid(content.lastIndexOf(u'\n', content.size() - 2) + 1);
        QVERIFY2(text.endsWith(body), qPrintable(text));
    }
};

QTEST_GUILESS_MAIN(TestNoteOptions)
#include "tst_markdown_noteoptions.moc"
