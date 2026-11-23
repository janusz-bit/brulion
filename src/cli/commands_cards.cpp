// Flashcards (docs/SYNTAX.md "Flashcards"): card list, card due, card show, card review (card
// grade), card notes and card review-note (notes reviewed as a whole), card stats — run by the
// built-in flashcards extension (src/core/extensions/builtin/fiszki.luau, its commands `list`,
// `show`, `grade`, `notes`, `review-note`, `stats`) over a dry-run NotebookHost: what it would
// write — the card's scheduling state, the review log's line, a note's frontmatter — goes through Call::edit like
// every other write of the command line (the lock, --dry-run, --diff).
#include "commands_p.h"

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

namespace ext = brulion::extensions;

const QString kCards = u"fiszki"_s;

/// The flashcards extension on an engine of its own over the notebook, its writes only computed.
struct Cards {
    std::unique_ptr<ext::NotebookHost> host;
    ext::Engine engine;
};

bool startCards(Call &call, Cards &c)
{
    Vault *v = call.vault();
    VaultIndex *index = v ? call.index() : nullptr;
    if (!index)
        return false;
    ext::NotebookHost::Options options;
    const QDateTime now = call.ctx.now();
    options.now = [now] { return now; };
    options.language = call.ctx.language();
    options.uiLanguage = call.ctx.language();
    options.dryRun = true;
    c.host = std::make_unique<ext::NotebookHost>(*v, *index, options);
    QString error;
    if (!c.engine.start(c.host.get(), ext::Limits{}, &error)) {
        call.setError(ExitInternal, u"engine_unavailable"_s, tr("The Luau engine did not start: %1").arg(error));
        return false;
    }
    const ext::Result loaded = c.engine.load(ext::builtinExtension(kCards));
    if (!loaded.ok()) {
        call.setError(ExitInternal, u"extension_error"_s,
                      tr("The flashcards extension does not load: %1").arg(loaded.error));
        return false;
    }
    return true;
}

/// The settings every command takes: the notebook's, --deck, --tags, --review-tags, --new-per-day, --retention.
QVariantMap settingsArgs(Call &call)
{
    // The notebook's own (config.json "flashcards": the plugin's card settings), then the options.
    QVariantMap args = call.vault() ? flashcardsSettings(call.vault()->config().flashcards) : QVariantMap();
    if (const QString deck = call.str(u"deck"_s); !deck.isEmpty())
        args.insert(u"deck"_s, deck);
    if (const QString tags = call.str(u"tags"_s); !tags.isEmpty())
        args.insert(u"tags"_s, tags);
    if (const QString reviewTags = call.str(u"reviewTags"_s); !reviewTags.isEmpty())
        args.insert(u"reviewTags"_s, reviewTags);
    if (call.args.contains(u"newPerDay"_s))
        args.insert(u"newPerDay"_s, call.integer(u"newPerDay"_s, 20));
    if (call.args.contains(u"retention"_s))
        args.insert(u"retention"_s, call.integer(u"retention"_s, 90));
    return args;
}

/// The extension's command `name` with `args` (its ctx.args): its result, or nullopt + call.error.
std::optional<QVariantMap> runCards(Call &call, Cards &c, const QString &name, const QVariantMap &args)
{
    const ext::Result r =
            c.engine.runCommand(kCards, name, {{u"page"_s, QString()}, {u"selection"_s, QString()}, {u"args"_s, args}});
    if (!r.ok()) {
        call.setError(ExitInternal, u"extension_error"_s,
                      tr("The flashcards extension failed: %1").arg(r.error.section(u'\n', 0, 0)));
        return std::nullopt;
    }
    return r.value.toMap();
}

QJsonArray arrayOf(const QVariant &value)
{
    return QJsonValue::fromVariant(value.metaType().id() == QMetaType::QVariantList ? value : QVariantList()).toArray();
}

/// One card as `--format text` shows it: "a1b2c3d4  Geografia  Stolica Peru → Lima  (review, 2026-11-23)".
QString cardLine(const QJsonObject &o)
{
    const QJsonObject m = o.value(u"memory"_s).toObject();
    const QString state = m.value(u"state"_s).toString();
    const QString when = m.value(u"due"_s).toString();
    QString question = o.value(u"question"_s).toString().simplified();
    QString answer = o.value(u"answer"_s).toString().simplified();
    return o.value(u"id"_s).toString() + u"  "_s + o.value(u"page"_s).toString() + u"  "_s + question + u" → "_s + answer
           + u"  ("_s + (when.isEmpty() ? state : state + u", "_s + when) + u')';
}

/// Each note the extension's grade changed — the card's, the review log's, a reviewed note's —
/// written as its own edit (Call::edit: the lock, --dry-run, --diff); false, with call.error, when
/// one failed. `writes` gets what each edit did.
bool writeCardChanges(Call &call, Cards &c, QJsonArray *writes)
{
    Vault *v = call.vault();
    if (!v)
        return false;
    for (const ext::NotebookHost::DryWrite &w : c.host->dryWrites()) {
        notebook::Notebook::WriteOptions options;
        options.mayCreate = !w.existed;
        const auto written = call.edit(
                w.path,
                [&](const QString &content, bool, QString *next) {
                    if (w.existed && content != w.before) {
                        call.setError(ExitConflict, u"concurrent_change"_s,
                                      tr("%1 kept changing while it was written (another program writes it): try again")
                                              .arg(w.path),
                                      {{u"path"_s, w.path}});
                        return false;
                    }
                    *next = w.after;
                    return true;
                },
                options);
        if (!written)
            return false;
        writes->append(call.writeJson(*written));
    }
    return true;
}

Result cardList(Call &call, bool dueOnly)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    QVariantMap args = settingsArgs(call);
    args.insert(u"due"_s, dueOnly);
    args.insert(u"limit"_s, std::clamp(call.integer(u"limit"_s, dueOnly ? 100 : 1000), 1, 10000));
    const auto r = runCards(call, c, u"list"_s, args);
    if (!r)
        return call.failed();
    const QJsonArray cards = arrayOf(r->value(u"cards"_s));
    QString text;
    for (const QJsonValue &v : cards)
        text += cardLine(v.toObject()) + u'\n';
    const QJsonObject counts = QJsonObject::fromVariantMap(r->value(u"counts"_s).toMap());
    if (cards.isEmpty())
        text = dueOnly ? tr("Nothing to study now") + u'\n'
                       : tr("No flashcards: write \"Question::Answer\" in a note tagged #flashcards") + u'\n';
    return call.ok(QJsonObject{{u"cards"_s, cards}, {u"counts"_s, counts}, {u"now"_s, r->value(u"now"_s).toString()}},
                   text);
}

Result cardShow(Call &call)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    QVariantMap args = settingsArgs(call);
    args.insert(u"id"_s, call.str(u"id"_s));
    const auto r = runCards(call, c, u"show"_s, args);
    if (!r)
        return call.failed();
    if (r->value(u"error"_s).toString() == u"card_not_found")
        return call.fail(ExitNotFound, u"card_not_found"_s, r->value(u"message"_s).toString(),
                         {{u"id"_s, call.str(u"id"_s)}});
    const QJsonObject o = QJsonObject::fromVariantMap(*r);
    return call.ok(o, o.value(u"question"_s).toString() + u"\n---\n"_s + o.value(u"answer"_s).toString() + u'\n');
}

Result cardReview(Call &call)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    QVariantMap args = settingsArgs(call);
    args.insert(u"id"_s, call.str(u"id"_s));
    args.insert(u"grade"_s, call.str(u"grade"_s));
    const auto r = runCards(call, c, u"grade"_s, args);
    if (!r)
        return call.failed();
    const QString error = r->value(u"error"_s).toString();
    if (error == u"bad_argument")
        return call.fail(ExitUsage, u"bad_argument"_s, tr("A grade is 1-4 or again, hard, good, easy (znowu, trudne, dobrze, łatwe)"),
                         {{u"grade"_s, call.str(u"grade"_s)}});
    if (error == u"card_not_found")
        return call.fail(ExitNotFound, u"card_not_found"_s, tr("No card %1").arg(call.str(u"id"_s)),
                         {{u"id"_s, call.str(u"id"_s)}});
    if (!error.isEmpty())
        return call.fail(ExitInternal, u"extension_error"_s, r->value(u"message"_s).toString());
    QJsonArray writes;
    if (!writeCardChanges(call, c, &writes))
        return call.failed();
    QJsonObject o = QJsonObject::fromVariantMap(*r);
    o.insert(u"writes"_s, writes);
    const QJsonObject after = o.value(u"after"_s).toObject();
    return call.ok(o, tr("%1: next %2 (%3)")
                              .arg(o.value(u"question"_s).toString().simplified(), after.value(u"due"_s).toString(),
                                   o.value(u"interval"_s).toString())
                              + u'\n');
}

/// One note as `--format text` shows it: "Notatka.md  due 2026-11-18  (3 days, ease 250)  → good 10 days".
QString noteLine(const QJsonObject &o)
{
    const QString status = o.value(u"status"_s).toString();
    QString line = o.value(u"path"_s).toString() + u"  "_s;
    if (status == u"new")
        line += tr("new");
    else
        line += (status == u"due" ? tr("due") : tr("scheduled")) + u' ' + o.value(u"due"_s).toString() + u" ("_s
                + tr("interval %1, ease %2").arg(o.value(u"interval"_s).toDouble()).arg(o.value(u"ease"_s).toDouble())
                + u')';
    return line;
}

Result cardNotes(Call &call, bool dueOnly)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    QVariantMap args = settingsArgs(call);
    args.insert(u"due"_s, dueOnly);
    args.insert(u"limit"_s, std::clamp(call.integer(u"limit"_s, 1000), 1, 10000));
    const auto r = runCards(call, c, u"notes"_s, args);
    if (!r)
        return call.failed();
    const QJsonArray notes = arrayOf(r->value(u"notes"_s));
    QString text;
    for (const QJsonValue &v : notes)
        text += noteLine(v.toObject()) + u'\n';
    if (notes.isEmpty())
        text = tr("No notes to review: tag a note #%1").arg(r->value(u"tags"_s).toStringList().value(0)) + u'\n';
    return call.ok(QJsonObject{{u"notes"_s, notes},
                               {u"counts"_s, QJsonObject::fromVariantMap(r->value(u"counts"_s).toMap())},
                               {u"today"_s, r->value(u"today"_s).toString()},
                               {u"tags"_s, QJsonArray::fromStringList(r->value(u"tags"_s).toStringList())}},
                   text);
}

Result cardReviewNote(Call &call)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    QVariantMap args = settingsArgs(call);
    args.insert(u"page"_s, call.str(u"page"_s));
    args.insert(u"grade"_s, call.str(u"grade"_s));
    const auto r = runCards(call, c, u"review-note"_s, args);
    if (!r)
        return call.failed();
    const QString error = r->value(u"error"_s).toString();
    if (error == u"bad_argument")
        return call.fail(ExitUsage, u"bad_argument"_s,
                         tr("A note's grade is hard, good or easy (2-4, trudne, dobrze, łatwe)"),
                         {{u"grade"_s, call.str(u"grade"_s)}});
    if (error == u"note_not_found")
        return call.fail(ExitNotFound, u"note_not_found"_s, tr("No note %1").arg(call.str(u"page"_s)),
                         {{u"page"_s, call.str(u"page"_s)}});
    if (error == u"not_review_note")
        return call.fail(ExitUsage, u"not_review_note"_s, r->value(u"message"_s).toString(),
                         {{u"page"_s, call.str(u"page"_s)}});
    if (!error.isEmpty())
        return call.fail(ExitInternal, u"extension_error"_s, r->value(u"message"_s).toString());
    QJsonArray writes;
    if (!writeCardChanges(call, c, &writes))
        return call.failed();
    QJsonObject o = QJsonObject::fromVariantMap(*r);
    o.insert(u"writes"_s, writes);
    const QJsonObject after = o.value(u"after"_s).toObject();
    return call.ok(o, tr("%1: next %2 (%3)")
                              .arg(o.value(u"page"_s).toString(), after.value(u"due"_s).toString(),
                                   o.value(u"interval"_s).toString())
                              + u'\n');
}

Result cardStats(Call &call)
{
    Cards c;
    if (!startCards(call, c))
        return call.failed();
    const auto r = runCards(call, c, u"stats"_s, settingsArgs(call));
    if (!r)
        return call.failed();
    const QJsonObject o = QJsonObject::fromVariantMap(*r);
    QString text = tr("%1 cards: %2 new, %3 learning, %4 young, %5 mature; %6 to study now, %7 reviewed today")
                           .arg(o.value(u"total"_s).toInt())
                           .arg(o.value(u"new"_s).toInt())
                           .arg(o.value(u"learning"_s).toInt())
                           .arg(o.value(u"young"_s).toInt())
                           .arg(o.value(u"mature"_s).toInt())
                           .arg(o.value(u"due"_s).toInt())
                           .arg(o.value(u"reviewedToday"_s).toInt());
    if (o.value(u"retention"_s).isDouble())
        text += u"; "_s + tr("retention %1 %").arg(qRound(o.value(u"retention"_s).toDouble() * 1000) / 10.0);
    return call.ok(o, text + u'\n');
}

} // namespace

void addCardCommands(QList<Command> &out)
{
    const QList<Arg> settings{
            option(u"deck"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Only this deck: tag:flashcards/spanish, page:Name or folder:Path (sub-decks too)")),
            option(u"tags"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The tags that make a note a deck (default: flashcards, fiszki)")),
            option(u"newPerDay"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "New cards a day (default 20)")),
            option(u"retention"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "Desired retention in percent (default 90)")),
    };
    const Arg id = positional(u"id"_s, ArgType::String,
                              QT_TRANSLATE_NOOP("brulion::cli", "The card's id, as card list gives it"));
    Command c;
    c.id = u"card.list"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The flashcards of the notebook: question, answer, deck, state"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Cards are the Obsidian Spaced Repetition plugin's (Question::Answer, Question:::Answer, a question, a "
            "line \"?\" and its answer, clozes ==like this== or {{c1::like this}}) in notes tagged #flashcards or "
            "#fiszki, and Logseq's #card blocks (docs/SYNTAX.md \"Flashcards\")."));
    c.args = settings;
    c.args << option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 1000)"));
    c.run = [](Call &call) { return cardList(call, false); };
    out.append(c);

    c = Command();
    c.id = u"card.due"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The flashcards to study now, in the order a session asks them"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Learning cards whose step is over, reviews due today, then new cards up to the day's limit. Grade each "
            "with card review <id> <grade> after asking its question and showing its answer."));
    c.args = settings;
    c.args << option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 100)"));
    c.run = [](Call &call) { return cardList(call, true); };
    out.append(c);

    c = Command();
    c.id = u"card.show"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "One flashcard: its question, answer, state and what each grade would schedule"));
    c.args = {id};
    c.args += settings;
    c.run = cardShow;
    out.append(c);

    c = Command();
    c.id = u"card.review"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Grade a flashcard: again, hard, good or easy (1-4)"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Schedules the card with FSRS and writes its state next to it in its note (the plugin's <!--SR:...--> "
            "comment, Logseq's card-* properties) and a line into the review log (Fiszki/Powtórki YYYY-MM)."));
    c.args = {id, positional(u"grade"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli", "again, hard, good, easy — or 1-4, znowu, trudne, dobrze, łatwe"))};
    c.args += settings;
    c.run = cardReview;
    c.writes = true;
    out.append(c);

    c = Command();
    c.id = u"card.grade"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Grade a flashcard (card review)"));
    c.args = {id, positional(u"grade"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli", "again, hard, good, easy — or 1-4, znowu, trudne, dobrze, łatwe"))};
    c.args += settings;
    c.run = cardReview;
    c.writes = true;
    c.mcp = false; // brulion_card_review covers it
    c.aliasOf = u"card.review"_s;
    out.append(c);

    c = Command();
    c.id = u"card.notes"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The notes reviewed as a whole (tagged #review): their state and the next intervals"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The Spaced Repetition plugin's note reviews: a note with the review tag (#review; the notebook's setting) "
            "is read whole, and its state is sr-due, sr-interval and sr-ease in its frontmatter "
            "(docs/SYNTAX.md \"Notes to review\"). With --due: what is to review now, due notes first, then new ones. "
            "Grade each with card review-note <page> <hard|good|easy>."));
    c.args = {option(u"reviewTags"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "The tags that make a note a note to review (default: review)")),
              option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 1000)")),
              option(u"due"_s, ArgType::Bool,
                     QT_TRANSLATE_NOOP("brulion::cli", "Only the notes to review now (due, then new)"))};
    c.run = [](Call &call) { return cardNotes(call, call.flag(u"due"_s)); };
    out.append(c);

    c = Command();
    c.id = u"card.review-note"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Review a note as a whole: hard, good or easy"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Schedules the note as the Spaced Repetition plugin does (its SM-2 variant with load balancing) and writes "
            "sr-due, sr-interval and sr-ease into its frontmatter, made when missing; nothing else of the note changes."));
    c.args = {positional(u"page"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The note: its name or its path")),
              positional(u"grade"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "hard, good, easy — or 2-4, trudne, dobrze, łatwe")),
              option(u"reviewTags"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "The tags that make a note a note to review (default: review)"))};
    c.run = cardReviewNote;
    c.writes = true;
    out.append(c);

    c = Command();
    c.id = u"card.stats"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Flashcard statistics: counts, reviews per day, retention, forecast"));
    c.args = settings;
    c.run = cardStats;
    out.append(c);
}

} // namespace brulion::cli
