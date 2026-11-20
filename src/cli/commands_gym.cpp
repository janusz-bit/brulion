// The gym (docs/SYNTAX.md §5 "Gym"): gym volume, gym weights, gym weigh, gym exercises, gym
// exercise — run by the built-in gym extension (src/core/extensions/builtin/silownia.luau, its
// commands `volume-report`, `weights`, `weight` and `catalog-search`) over a dry-run NotebookHost:
// what `gym weigh` would write — a weigh-in at the end of a daily note — goes through Call::edit
// like every other write of the command line (the lock, --dry-run, --diff, a daily note made from
// its template).
#include "commands_p.h"

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

namespace ext = brulion::extensions;

const QString kGym = u"silownia"_s;

/// The gym extension on an engine of its own over the notebook, its writes only computed.
struct Gym {
    std::unique_ptr<ext::NotebookHost> host;
    ext::Engine engine;
};

bool startGym(Call &call, Gym &g)
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
    g.host = std::make_unique<ext::NotebookHost>(*v, *index, options);
    QString error;
    if (!g.engine.start(g.host.get(), ext::Limits{}, &error)) {
        call.setError(ExitInternal, u"engine_unavailable"_s, tr("The Luau engine did not start: %1").arg(error));
        return false;
    }
    const ext::Result loaded = g.engine.load(ext::builtinExtension(kGym));
    if (!loaded.ok()) {
        call.setError(ExitInternal, u"extension_error"_s, tr("The gym extension does not load: %1").arg(loaded.error));
        return false;
    }
    // The device's unit (Settings → Extensions → Gym) is the app's; here --unit says it.
    if (const QString unit = call.str(u"unit"_s); unit == u"kg" || unit == u"lb")
        g.engine.evaluate(kGym, u"b.setSetting(\"unit\", \"%1\")"_s.arg(unit), u"unit"_s);
    return true;
}

/// The extension's command `name` with `args` (its ctx.args): its result, or nullopt + call.error.
std::optional<QVariantMap> runGym(Call &call, Gym &g, const QString &name, const QVariantMap &args)
{
    const ext::Result r =
            g.engine.runCommand(kGym, name, {{u"page"_s, QString()}, {u"selection"_s, QString()}, {u"args"_s, args}});
    if (!r.ok()) {
        call.setError(ExitInternal, u"extension_error"_s,
                      tr("The gym extension failed: %1").arg(r.error.section(u'\n', 0, 0)));
        return std::nullopt;
    }
    return r.value.toMap();
}

QJsonArray arrayOf(const QVariant &value)
{
    return QJsonValue::fromVariant(value.metaType().id() == QMetaType::QVariantList ? value : QVariantList()).toArray();
}

/// A number as the command line writes it for people: "1537.5", "82".
QString number(double value)
{
    return QString::number(std::round(value * 10) / 10, 'f', value == std::round(value) ? 0 : 1);
}

Result gymVolume(Call &call)
{
    Gym g;
    if (!startGym(call, g))
        return call.failed();
    const int weeks = std::clamp(call.integer(u"weeks"_s, 12), 1, 104);
    const auto r = runGym(call, g, u"volume-report"_s, {{u"weeks"_s, weeks}});
    if (!r)
        return call.failed();
    const QJsonObject o = QJsonObject::fromVariantMap(*r);
    const QString unit = o.value(u"unit"_s).toString();
    QString text = tr("This week: %1 %2 (average %3 %2), %4 sets")
                           .arg(number(o.value(u"current"_s).toDouble()), unit,
                                o.value(u"average"_s).isDouble() ? number(o.value(u"average"_s).toDouble()) : u"—"_s)
                           .arg(arrayOf(r->value(u"sets"_s)).last().toDouble())
                   + u'\n';
    for (const QJsonValue &v : o.value(u"muscles"_s).toArray()) {
        const QJsonObject m = v.toObject();
        text += u"  "_s + m.value(u"name"_s).toString() + u": "_s + number(m.value(u"current"_s).toDouble()) + u' '
                + unit + u" ("_s + tr("%1 sets").arg(m.value(u"currentSets"_s).toDouble()) + u", "_s
                + tr("average %1 %2")
                          .arg(m.value(u"average"_s).isDouble() ? number(m.value(u"average"_s).toDouble()) : u"—"_s,
                               unit)
                + u")\n"_s;
    }
    const QJsonArray unassigned = o.value(u"unassigned"_s).toArray();
    if (!unassigned.isEmpty()) {
        QStringList names;
        for (const QJsonValue &n : unassigned)
            names << n.toString();
        text += tr("No muscles known: %1").arg(names.join(u", "_s)) + u'\n';
    }
    return call.ok(o, text);
}

Result gymWeights(Call &call)
{
    Gym g;
    if (!startGym(call, g))
        return call.failed();
    QVariantMap args;
    for (const QString &key : {u"from"_s, u"to"_s}) {
        if (!call.has(key))
            continue;
        const auto day = call.date(call.str(key));
        if (!day)
            return call.failed();
        args.insert(key, day->toString(Qt::ISODate));
    }
    const auto r = runGym(call, g, u"weights"_s, args);
    if (!r)
        return call.failed();
    const QJsonObject o = QJsonObject::fromVariantMap(*r);
    QString text;
    for (const QJsonValue &v : o.value(u"entries"_s).toArray()) {
        const QJsonObject e = v.toObject();
        text += e.value(u"date"_s).toString() + u"  "_s + number(e.value(u"weight"_s).toDouble()) + u' '
                + e.value(u"unit"_s).toString();
        if (e.value(u"fat"_s).isDouble())
            text += u"  "_s + number(e.value(u"fat"_s).toDouble()) + u" %"_s;
        if (!e.value(u"notes"_s).toString().isEmpty())
            text += u"  "_s + e.value(u"notes"_s).toString();
        text += u'\n';
    }
    if (o.value(u"trend"_s).isDouble())
        text += tr("Trend %1 %2").arg(number(o.value(u"trend"_s).toDouble()), o.value(u"unit"_s).toString()) + u'\n';
    else
        text = tr("No weigh-ins: \"- 82.5 kg #weight\" (#waga) in a daily note") + u'\n';
    return call.ok(o, text);
}

Result gymWeigh(Call &call)
{
    Gym g;
    if (!startGym(call, g))
        return call.failed();
    QVariantMap args{{u"text"_s, call.str(u"weight"_s)}};
    if (call.has(u"fat"_s))
        args.insert(u"text"_s, QString(call.str(u"weight"_s) + u' ' + call.str(u"fat"_s) + u" %"_s));
    if (call.has(u"note"_s))
        args.insert(u"notes"_s, call.str(u"note"_s));
    const auto day = call.date(call.str(u"date"_s));
    if (!day)
        return call.failed();
    args.insert(u"date"_s, day->toString(Qt::ISODate));
    const auto r = runGym(call, g, u"weight"_s, args);
    if (!r)
        return call.failed();
    if (r->value(u"tone"_s).toString() != u"success")
        return call.fail(ExitUsage, u"bad_argument"_s, r->value(u"message"_s).toString(),
                         {{u"weight"_s, call.str(u"weight"_s)}});
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    QJsonArray writes;
    for (const ext::NotebookHost::DryWrite &w : g.host->dryWrites()) {
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
            return call.failed();
        writes.append(call.writeJson(*written));
    }
    const QJsonObject o{{u"date"_s, r->value(u"date"_s).toString()},
                        {u"line"_s, r->value(u"line"_s).toString()},
                        {u"writes"_s, writes}};
    return call.ok(o, r->value(u"date"_s).toString() + u": "_s + r->value(u"line"_s).toString() + u'\n');
}

/// One exercise as `--format text` shows it: "back-squat  Przysiad ze sztangą — czworogłowe uda · sztanga".
QString exerciseLine(const QJsonObject &e)
{
    QString line = e.value(u"id"_s).toString() + u"  "_s + e.value(u"name"_s).toString() + u" — "_s
                   + e.value(u"muscles"_s).toString();
    if (!e.value(u"equipmentName"_s).toString().isEmpty())
        line += u" · "_s + e.value(u"equipmentName"_s).toString();
    return line;
}

Result gymExercises(Call &call)
{
    Gym g;
    if (!startGym(call, g))
        return call.failed();
    QVariantMap args{{u"query"_s, call.str(u"query"_s)},
                     {u"limit"_s, std::clamp(call.integer(u"limit"_s, 20), 1, 2000)}};
    for (const auto &[option, key] : {std::pair{u"muscle"_s, u"muscle"_s}, std::pair{u"equipment"_s, u"equipment"_s},
                                      std::pair{u"kind"_s, u"category"_s}}) {
        if (call.has(option))
            args.insert(key, call.str(option));
    }
    const auto r = runGym(call, g, u"catalog-search"_s, args);
    if (!r)
        return call.failed();
    if (const QString unknown = r->value(u"unknown"_s).toString(); !unknown.isEmpty()) {
        const QString option = unknown == u"category" ? u"kind"_s : unknown;
        return call.fail(ExitUsage, u"bad_argument"_s,
                         tr("The catalog has no such %1: %2").arg(option, call.str(option)), {{u"option"_s, option}});
    }
    const QJsonObject o = QJsonObject::fromVariantMap(*r);
    QString text;
    for (const QJsonValue &v : o.value(u"exercises"_s).toArray())
        text += exerciseLine(v.toObject()) + u'\n';
    text += tr("%1 found").arg(o.value(u"total"_s).toInt()) + u'\n';
    return call.ok(o, text);
}

Result gymExercise(Call &call)
{
    Gym g;
    if (!startGym(call, g))
        return call.failed();
    const auto r = runGym(call, g, u"catalog-search"_s, {{u"id"_s, call.str(u"id"_s)}});
    if (!r)
        return call.failed();
    if (r->contains(u"unknown"_s))
        return call.fail(ExitNotFound, u"exercise_not_found"_s,
                         tr("No exercise %1 in the catalog").arg(call.str(u"id"_s)), {{u"id"_s, call.str(u"id"_s)}});
    const QJsonObject e = arrayOf(r->value(u"exercises"_s)).first().toObject();
    QString text = exerciseLine(e) + u'\n';
    int n = 0;
    for (const QJsonValue &step : e.value(u"instructions"_s).toArray())
        text += QString::number(++n) + u". "_s + step.toString() + u'\n';
    return call.ok(e, text);
}

} // namespace

void addGymCommands(QList<Command> &out)
{
    Arg units = option(u"unit"_s, ArgType::Enum,
                       QT_TRANSLATE_NOOP("brulion::cli", "Body weight and volume in kg or lb (default kg)"));
    units.values = {u"kg"_s, u"lb"_s};
    Command c;
    c.id = u"gym.volume"_s;
    c.summary = cliText(
            QT_TRANSLATE_NOOP("brulion::cli", "Weekly volume per muscle: sets × reps × weight of the workouts"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The done sets of the workouts (#trening, #workout) in the daily notes, per muscle and week: every primary "
            "muscle of an exercise gets its volume and sets, every secondary one half; bodyweight sets count with the "
            "body weight of the log. The muscles come from the exercise's page (miesnie::, muscles::) or the "
            "catalog."));
    c.args = {option(u"weeks"_s, ArgType::Int,
                     QT_TRANSLATE_NOOP("brulion::cli", "This many weeks up to this one (default 12)")),
              units};
    c.run = gymVolume;
    out.append(c);

    c = Command();
    c.id = u"gym.weights"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The body weight log: weigh-ins, the trend, its changes"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "A weigh-in is a journal item tagged #waga or #weight: \"- 82,5 kg 18 % rano #waga\". The trend is an "
            "average smoothed by 10 % a day."));
    c.args = {option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "From this day")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "Up to this day")), units};
    c.run = gymWeights;
    out.append(c);

    c = Command();
    c.id = u"gym.weigh"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Log a weigh-in in the daily note"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Writes \"- 82,5 kg #waga\" (\"- 82.5 kg #weight\" in English) at the end of the day's note, made from the "
            "journal template when missing."));
    c.args = {positional(u"weight"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "The weight: 82.5, 82,5 kg, 181 lb (with --unit: its unit)"),
                         true),
              option(u"fat"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Body fat in per cent")),
              option(u"note"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "A note after it (morning, after a run)")),
              option(u"date"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The day (default: today)")), units};
    c.run = gymWeigh;
    c.writes = true;
    out.append(c);

    c = Command();
    c.id = u"gym.exercises"_s;
    c.summary =
            cliText(QT_TRANSLATE_NOOP("brulion::cli", "Search the exercise catalog by name, muscle, equipment, kind"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Brulion's exercises and free-exercise-db's (the Unlicense), with Polish names for the common ones. Names "
            "match in any case and without diacritics; filters take an id or a name in either language (chest, klatka "
            "piersiowa; dumbbell, hantle; stretching, rozciąganie)."));
    c.args = {positional(u"query"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Words of its name"), false,
                         true),
              option(u"muscle"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A primary muscle")),
              option(u"equipment"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Its equipment")),
              option(u"kind"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "strength, stretching, cardio, …")),
              option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 20)"))};
    c.run = gymExercises;
    out.append(c);

    c = Command();
    c.id = u"gym.exercise"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "One exercise of the catalog with its instructions"));
    c.args = {positional(u"id"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "Its id, as gym exercises gives it"), true)};
    c.run = gymExercise;
    out.append(c);
}

} // namespace brulion::cli
