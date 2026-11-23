// The Tasks view's rows with macros of extensions (TaskListModel.host; docs/EXTENSIONS.md
// "Macros"): without a host a macro shows as written; with one, what the host computed for the
// task's note ("…" until then, a placeholder while its extension waits for approval), escaped;
// a new value renders only the rows that show a macro again — a hidden list once it is shown —,
// and a macro in a code span stays text. FakeEditorHost stands in for the Workspace.
#include "fakeeditorhost.h"
#include "planningtest.h"
#include "tasklistmodel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::FakeEditorHost;
using brulion::markdown::MacroPiece;
using brulion::markdown::MacroView;
using brulion::planning::TaskListModel;
using brulion::planning::test::makeIndex;
using brulion::planning::test::ModelMirror;

namespace {

const QString kSquat = u"Siłownia.md:0"_s;
const QString kRowing = u"Siłownia.md:1"_s;

MacroView value(const QString &text)
{
    MacroView view;
    view.pieces = {MacroPiece{MacroPiece::Kind::Text, text, {}, {}, false}};
    return view;
}

} // namespace

class TestPlanningMacros : public QObject {
    Q_OBJECT

    std::unique_ptr<brulion::VaultIndex> m_index;
    FakeEditorHost m_host;

    QString html(const TaskListModel &m, const QString &key) const
    {
        return m.data(m.index(m.rowOfKey(key)), TaskListModel::HtmlRole).toString();
    }

private Q_SLOTS:
    void init()
    {
        m_index = makeIndex({{u"Siłownia.md"_s, u"- [ ] Przysiad {{ext:silownia/rekord Przysiad}} 📅 2026-10-15\n"
                                                 "- [ ] Wiosłowanie `{{ext:silownia/rekord Wiosło}}` 📅 2026-10-15\n"
                                                 "- [ ] Bieg — żółw 📅 2026-10-15\n"_s}});
        // (The fake host computes macros once one is set; the others show "…" until set.)
        m_host.setMacroView(u"silownia/inne"_s, value(u"inne"_s));
    }

    void withoutAHostAsWritten()
    {
        TaskListModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(m_index.get());
        QVERIFY(m.rowOfKey(kSquat) >= 0);
        QVERIFY2(html(m, kSquat).contains(u"{{ext:silownia/rekord Przysiad}}"_s), qPrintable(html(m, kSquat)));
        // Given a host, the rows rendered before show its values.
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        m.setHost(&m_host);
        QVERIFY(!changed.isEmpty());
        QVERIFY2(!html(m, kSquat).contains(u"{{ext:"_s), qPrintable(html(m, kSquat)));
        QVERIFY2(html(m, kSquat).contains(u"…"_s), qPrintable(html(m, kSquat)));
    }

    void valuesFollowTheHost()
    {
        TaskListModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.setToday(u"2026-10-15"_s);
        m.setHost(&m_host);
        m.setVaultIndex(m_index.get());
        QVERIFY2(html(m, kSquat).contains(u"…"_s), qPrintable(html(m, kSquat)));
        // In a code span a macro is text.
        QVERIFY2(html(m, kRowing).contains(u"{{ext:silownia/rekord Wiosło}}"_s), qPrintable(html(m, kRowing)));
        // (Every row read once: only the rows that showed a macro are asked again.)
        ModelMirror mirror(&m, [&m](int row) { return m.data(m.index(row), TaskListModel::HtmlRole).toString(); });

        m_host.setMacroView(u"silownia/rekord Przysiad"_s, value(u"120 kg <script>"_s));
        QCOMPARE(mirror.changedRows, 1);
        QCOMPARE(mirror.rows, mirror.current());
        QVERIFY2(html(m, kSquat).contains(u"120 kg &lt;script&gt;"_s), qPrintable(html(m, kSquat)));
        // The same value again: no row changes.
        m_host.setMacroView(u"silownia/rekord Przysiad"_s, value(u"120 kg <script>"_s));
        QCOMPARE(mirror.changedRows, 1);

        // Waiting for approval on this device: the placeholder, as in the editor.
        MacroView unapproved;
        unapproved.state = MacroView::State::Unapproved;
        m_host.setMacroView(u"silownia/rekord Przysiad"_s, unapproved);
        QCOMPARE(mirror.changedRows, 2);
        QVERIFY2(html(m, kSquat).contains(u"uruchomić?"_s), qPrintable(html(m, kSquat)));

        // Hidden: nothing is rendered until the list is shown again.
        m.setActive(false);
        m_host.setMacroView(u"silownia/rekord Przysiad"_s, value(u"125 kg"_s));
        QCOMPARE(mirror.changedRows, 2);
        m.setActive(true);
        QCOMPARE(mirror.changedRows, 3);
        QCOMPARE(mirror.rows, mirror.current());
        QVERIFY2(html(m, kSquat).contains(u"125 kg"_s), qPrintable(html(m, kSquat)));
    }
};

QTEST_MAIN(TestPlanningMacros)
#include "tst_planning_macros.moc"
