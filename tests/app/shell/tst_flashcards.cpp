// The Flashcards view as the app draws it (docs/UI.md "Flashcards"): an ExtensionViewModel over
// the real built-in extension `fiszki` (src/core/extensions/builtin/fiszki.luau) switched on in a
// WorkspaceExtensions over a small notebook — the decks with their counts and tiles, the review
// session driven by its keyboard shortcuts (Space shows the answer, 3 grades it, U undoes), the
// card's line in its note and the review log written and put back, Markdown lines rendered as rich
// text, the settings the app gives with every call (new cards a day), the due count the sidebar
// shows, "open" buttons and menus that ask for another view; the extension off: nothing drawn.

#include "workspaceextensions.h"

#include "extensionviewmodel.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;

namespace {

const QString kDeck = u"Nauka/Geografia.md"_s;
const QString kDeckText = u"---\ntags: [fiszki]\n---\n# Geografia\n\nStolica **Peru**::Lima\nStolica Chile::Santiago\n"
                          "Najwyższe są ==Rysy==.\n"_s;

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TestShellFlashcards : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<WorkspaceExtensions> m_ext;
    QStringList m_messages;

    QString disk(const QString &path) const { return readFile(m_dir + u'/' + path); }

    void open(const QHash<QString, QString> &files)
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
        m_messages.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = m_dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        QVERIFY(m_vault->open());
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        m_ext = std::make_unique<WorkspaceExtensions>([] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); });
        connect(m_ext.get(), &WorkspaceExtensions::message, this,
                [this](const QString &text, const QString &kind) { m_messages << kind + u": "_s + text; });
        connect(m_vault.get(), &Vault::changed, this, [this](const QList<VaultChange> &changes) {
            QStringList changed;
            for (const VaultChange &c : changes) {
                if (c.type == ChangeType::Deleted) {
                    m_index->remove(c.path);
                } else if (paths::isMarkdown(c.path)) {
                    bool ok = false;
                    const QString content = m_vault->readText(c.path, &ok);
                    if (ok)
                        m_index->update(c.path, content);
                }
                changed << c.path;
            }
            m_ext->notesChanged(changed);
        });
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->setLanguage(u"pl"_s);
        m_ext->setFlashcardSettings({{u"tags"_s, u"flashcards, fiszki"_s}, {u"newPerDay"_s, 20}, {u"retention"_s, 0.9},
                                     {u"fuzz"_s, false}});
        m_ext->setEnabled({u"fiszki"_s});
        m_ext->indexReady();
    }

    void draw(ExtensionViewModel &model, const QString &view)
    {
        model.setExtensions(m_ext.get());
        model.setExtension(u"fiszki"_s);
        model.setView(view);
        model.refresh();
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
    }

    static QVariant role(const ExtensionViewModel &model, int row, int role)
    {
        return model.data(model.index(row), role);
    }
    static QStringList types(const ExtensionViewModel &model)
    {
        QStringList out;
        for (int i = 0; i < model.rowCount(); ++i)
            out << role(model, i, ExtensionViewModel::TypeRole).toString();
        return out;
    }
    static int rowOf(const ExtensionViewModel &model, const QString &type, int from = 0)
    {
        for (int i = from; i < model.rowCount(); ++i)
            if (role(model, i, ExtensionViewModel::TypeRole).toString() == type)
                return i;
        return -1;
    }
    static ExtensionControlsModel *controlsOf(const ExtensionViewModel &model, int row)
    {
        return qobject_cast<ExtensionControlsModel *>(role(model, row, ExtensionViewModel::ControlsRole).value<QObject *>());
    }
    /// The labels and keys of the buttons of every row ("Pokaż odpowiedź [space]").
    static QStringList buttons(const ExtensionViewModel &model)
    {
        QStringList out;
        for (int i = 0; i < model.rowCount(); ++i) {
            const ExtensionControlsModel *c = controlsOf(model, i);
            for (int k = 0; c && k < c->rowCount(); ++k) {
                if (c->data(c->index(k), ExtensionControlsModel::TypeRole).toString() != u"button")
                    continue;
                out << c->data(c->index(k), ExtensionControlsModel::TextRole).toString() + u" ["_s
                               + c->data(c->index(k), ExtensionControlsModel::KeyRole).toString() + u']';
            }
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // The decks: tiles of numbers, "Study all" as a primary button with Enter, a row per deck.
    void decks()
    {
        open({{kDeck, kDeckText}, {u"Logseq.md"_s, u"- Co znaczy „brulion”? #card\n  - zeszyt\n"_s}});
        ExtensionViewModel model;
        draw(model, u"decks"_s);
        const QStringList t = types(model);
        QCOMPARE(t.first(), u"stats"_s);
        const QVariantList tiles = role(model, 0, ExtensionViewModel::ItemsRole).toList();
        QCOMPARE(tiles.size(), 4);
        QCOMPARE(tiles[1].toMap().value(u"value"_s).toString(), u"4"_s); // new: two cards, a cloze, the Logseq card
        QCOMPARE(tiles[1].toMap().value(u"label"_s).toString(), u"Nowe"_s);
        // "Ucz się wszystkich": an open drawn as a primary button, its key Enter.
        const ExtensionControlsModel *study = controlsOf(model, 1);
        QVERIFY(study);
        QCOMPARE(study->data(study->index(0), ExtensionControlsModel::StyledRole).toBool(), true);
        QCOMPARE(study->data(study->index(0), ExtensionControlsModel::PrimaryRole).toBool(), true);
        QCOMPARE(study->data(study->index(0), ExtensionControlsModel::KeyRole).toString(), u"enter"_s);
        QSignalSpy views(&model, &ExtensionViewModel::viewRequested);
        QVERIFY(model.activateKey(u"enter"_s));
        QCOMPARE(views.size(), 1);
        QCOMPARE(views[0][0].toString(), u"review"_s);
        const QVariantMap route = model.tabRoute(u"flashcards"_s, u"review"_s, views[0][1].toMap());
        QCOMPARE(route.value(u"view"_s).toString(), u"flashcards"_s);
        QCOMPARE(route.value(u"tab"_s).toString(), u"review"_s);
        QVERIFY(!model.activateKey(u"x"_s)); // no button has it
        // The decks by tag: #card and #fiszki, each a row with its counts and a menu.
        QCOMPARE(t.count(u"row"_s), 3); // the buttons, #card, #fiszki
        // The menu of a deck offers its review and its statistics as other views.
        const int deckRow = rowOf(model, u"row"_s, rowOf(model, u"heading"_s));
        QVERIFY(deckRow > 0);
        const QStringList menu = model.menuLabels(deckRow, 1, QString());
        QCOMPARE(menu, (QStringList{u"Ucz się"_s, u"Ucz się z wyprzedzeniem"_s, u"Statystyki"_s}));
        model.activateMenuItem(deckRow, 1, QString(), 2);
        QCOMPARE(views.size(), 2);
        QCOMPARE(views[1][0].toString(), u"stats"_s);
    }

    // A session by the keyboard: Space shows the answer, 3 grades it (the card's line and the
    // log), U undoes (both as they were), the answer of the card graded again shows.
    void reviewByKeys()
    {
        open({{kDeck, kDeckText}});
        ExtensionViewModel model;
        draw(model, u"review"_s);
        QCOMPARE(types(model).first(), u"progress"_s);
        const int question = rowOf(model, u"markdown"_s);
        QVERIFY(question > 0);
        QCOMPARE(role(model, question, ExtensionViewModel::SizeRole).toString(), u"large"_s);
        // The question as rich text: its bold, escaped.
        QVERIFY2(role(model, question, ExtensionViewModel::HtmlRole).toString().contains(u"<b>Peru</b>"_s),
                 qPrintable(role(model, question, ExtensionViewModel::HtmlRole).toString()));
        QCOMPARE(role(model, question, ExtensionViewModel::TitleRole).toString(), u"Stolica Peru"_s);
        QCOMPARE(buttons(model), QStringList{u"Pokaż odpowiedź [space]"_s});
        QVERIFY(model.activateKey(u"space"_s));
        QTRY_VERIFY(buttons(model).size() == 4);
        QCOMPARE(buttons(model), (QStringList{u"Znowu [1]"_s, u"Trudne [2]"_s, u"Dobrze [3]"_s, u"Łatwe [4]"_s}));
        QVERIFY(rowOf(model, u"divider"_s) > 0);
        // The grades' second lines: their next intervals.
        const ExtensionControlsModel *grades = controlsOf(model, rowOf(model, u"row"_s));
        QCOMPARE(grades->data(grades->index(2), ExtensionControlsModel::HintRole).toString(), u"10 min"_s);
        QCOMPARE(grades->data(grades->index(0), ExtensionControlsModel::ToneRole).toString(), u"danger"_s);
        QVERIFY(model.activateKey(u"3"_s));
        QVERIFY(disk(kDeck).contains(u"Stolica **Peru**::Lima <!--SR:!fsrs,"_s));
        QVERIFY(disk(u"Fiszki/Powtórki 2026-11.md"_s).contains(u"- 2026-11-20 18:30 dobrze · nowa · Stolica Peru"_s));
        QTRY_COMPARE(role(model, rowOf(model, u"markdown"_s), ExtensionViewModel::TitleRole).toString(), u"Stolica Chile"_s);
        // Undo: both notes as they were, the card back with its answer.
        QVERIFY(buttons(model).contains(u"Cofnij [u]"_s));
        QVERIFY(model.activateKey(u"u"_s));
        QCOMPARE(disk(kDeck), kDeckText);
        QVERIFY(!disk(u"Fiszki/Powtórki 2026-11.md"_s).contains(u"Stolica Peru"_s));
        QTRY_COMPARE(role(model, rowOf(model, u"markdown"_s), ExtensionViewModel::TitleRole).toString(), u"Stolica Peru"_s);
        QTRY_VERIFY(buttons(model).contains(u"Dobrze [3]"_s));
        QVERIFY(m_messages.contains(u"info: Cofnięto ocenę"_s));
    }

    // The settings the app gives (new cards a day) and the due count of the sidebar.
    void settingsAndDueCount()
    {
        open({{kDeck, kDeckText}});
        QSignalSpy due(m_ext.get(), &WorkspaceExtensions::flashcardsDueChanged);
        m_ext->refreshFlashcardsDue();
        QCOMPARE(m_ext->flashcardsDue(), 3); // two cards and a cloze, all new
        m_ext->setFlashcardSettings({{u"newPerDay"_s, 2}});
        m_ext->refreshFlashcardsDue();
        QCOMPARE(m_ext->flashcardsDue(), 2);
        ExtensionViewModel model;
        draw(model, u"decks"_s);
        QCOMPARE(role(model, 0, ExtensionViewModel::ItemsRole).toList().value(1).toMap().value(u"value"_s).toString(), u"2"_s);
        QVERIFY(due.size() >= 2);
        // Off: nothing counted, nothing drawn.
        m_ext->setEnabled({});
        m_ext->refreshFlashcardsDue();
        QCOMPARE(m_ext->flashcardsDue(), 0);
        model.refresh();
        QCOMPARE(model.rowCount(), 0);
    }
};

QTEST_MAIN(TestShellFlashcards)
#include "tst_flashcards.moc"
