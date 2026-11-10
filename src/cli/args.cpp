// The command line: global options, the command's words, its arguments (docs/CLI.md "Usage").
#include "cli_p.h"

#include <QHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// Global options that take a value, with the field they fill.
QString *globalValue(Globals &g, const QString &name)
{
    if (name == u"--vault")
        return &g.vault;
    if (name == u"--state-dir")
        return &g.stateDir;
    if (name == u"--format")
        return &g.format;
    if (name == u"--now")
        return &g.now;
    if (name == u"--lang")
        return &g.lang;
    return nullptr;
}

bool *globalFlag(Globals &g, const QString &name)
{
    if (name == u"--no-history")
        return &g.noHistory;
    if (name == u"--compact")
        return &g.compact;
    if (name == u"--dry-run")
        return &g.dryRun;
    if (name == u"--diff")
        return &g.diff;
    if (name == u"--read-only")
        return &g.readOnly;
    if (name == u"--allow-destructive")
        return &g.allowDestructive;
    if (name == u"--stdin")
        return &g.stdinText;
    if (name == u"-v" || name == u"--verbose")
        return &g.verbose;
    if (name == u"-h" || name == u"--help")
        return &g.help;
    return nullptr;
}

Error usage(const QString &code, const QString &message, const QJsonObject &details = {})
{
    return Error{ExitUsage, code, message, details};
}

/// An option as written: "--name", or "--name=value" (a long option with its value).
struct Option {
    QString name;
    std::optional<QString> value;
};

Option splitOption(const QString &a)
{
    if (const qsizetype eq = a.indexOf(u'='); eq > 0 && a.startsWith(u"--"))
        return {a.left(eq), a.mid(eq + 1)};
    return {a, std::nullopt};
}

/// A flag's value: on, unless "--flag=…" says something other than true or 1.
bool flagValue(const Option &option)
{
    return !option.value || *option.value == u"true" || *option.value == u"1";
}

/// Do `words` begin a command's id ("page", "page read")?
bool isCommandPrefix(const QStringList &words)
{
    const QString prefix = words.join(u'.');
    return std::ranges::any_of(
            commands(), [&prefix](const Command &c) { return c.id == prefix || c.id.startsWith(prefix + u'.'); });
}

/// Commands whose first word alone means a subcommand ("journal" = "journal read").
const QHash<QString, QString> &defaultSubcommands()
{
    static const QHash<QString, QString> table{{u"journal"_s, u"journal.read"_s},
                                               {u"week"_s, u"week.read"_s},
                                               {u"month"_s, u"month.read"_s},
                                               {u"app"_s, u"app.open"_s}};
    return table;
}

/// The command the longest run of `words` names (else the first word's default subcommand) and
/// how many of the words its name takes; {nullptr, 0} when none.
std::pair<const Command *, qsizetype> commandOf(const QStringList &words)
{
    for (qsizetype k = words.size(); k >= 1; --k) {
        if (const Command *c = findCommand(words.first(k).join(u'.')))
            return {c, k};
    }
    if (const auto it = defaultSubcommands().constFind(words.value(0)); it != defaultSubcommands().cend())
        return {findCommand(*it), 1};
    return {nullptr, 0};
}

/// The values of --format, --lang and --now.
std::optional<Error> checkGlobals(const Globals &g)
{
    if (g.format != u"json" && g.format != u"text")
        return usage(u"bad_format"_s, tr("--format is json or text"), {{u"format"_s, g.format}});
    if (!g.lang.isEmpty() && g.lang != u"pl" && g.lang != u"en")
        return usage(u"bad_lang"_s, tr("--lang is pl or en"), {{u"lang"_s, g.lang}});
    if (!g.now.isEmpty() && !QDateTime::fromString(g.now, u"yyyy-MM-dd HH:mm"_s).isValid())
        return usage(u"bad_now"_s, tr("--now takes \"YYYY-MM-DD HH:mm\" (local time)"), {{u"now"_s, g.now}});
    return std::nullopt;
}

} // namespace

Invocation parseArguments(const QStringList &arguments)
{
    Invocation inv;
    Globals &g = inv.globals;
    const qsizetype n = arguments.size();
    // The value of `option` at position i: after its '=', else the next argument (i advanced to
    // it); nullopt with inv.error when there is none.
    const auto valueOf = [&](qsizetype &i, const Option &option) -> std::optional<QString> {
        if (option.value)
            return option.value;
        if (i + 1 < n)
            return arguments.at(++i);
        inv.error = usage(u"missing_value"_s, tr("%1 needs a value").arg(option.name), {{u"option"_s, option.name}});
        return std::nullopt;
    };
    // A global option at position i: true when handled (i advanced past its value).
    const auto global = [&](qsizetype &i) -> bool {
        const Option option = splitOption(arguments.at(i));
        if (QString *field = globalValue(g, option.name)) {
            if (const auto value = valueOf(i, option))
                *field = *value;
            return true;
        }
        if (bool *flag = globalFlag(g, option.name)) {
            *flag = flagValue(option);
            return true;
        }
        return false;
    };

    // The command's words come first (after global options): the longest run naming a command.
    QStringList commandWords;
    qsizetype i = 0;
    for (; i < n && !inv.error; ++i) {
        const QString &a = arguments.at(i);
        if (a.startsWith(u'-') && a != u"-") {
            if (a == u"--version" && commandWords.isEmpty()) {
                commandWords.append(u"version"_s);
                continue;
            }
            if (global(i))
                continue;
            break; // the command's own options start here
        }
        if (!isCommandPrefix(commandWords + QStringList{a}))
            break;
        commandWords.append(a);
    }
    if (inv.error)
        return inv;
    const auto [command, used] = commandOf(commandWords);
    if (g.help) {
        inv.command = command;
        inv.helpTopic = command ? command->id : QString();
        if (!command && (i < n || !commandWords.isEmpty())) {
            const QString topic = (commandWords + arguments.mid(i)).join(u' ');
            inv.error = usage(u"unknown_command"_s, tr("Unknown command: %1").arg(topic), {{u"command"_s, topic}});
        }
        return inv;
    }
    if (!command) {
        const QString given = (commandWords + arguments.mid(i, 1)).join(u' ');
        if (given.isEmpty())
            inv.error = usage(u"no_command"_s, tr("No command given (try: help)"));
        else
            inv.error = usage(u"unknown_command"_s, tr("Unknown command: %1 (try: help)").arg(given),
                              {{u"command"_s, given}});
        return inv;
    }
    inv.command = command;

    // Its arguments: options (anywhere), positional words in order, global options too. Words the
    // command's name did not take are its first positional arguments.
    QStringList positional = commandWords.mid(used);
    QJsonObject args;
    bool dashdash = false;
    for (; i < n; ++i) {
        const QString &a = arguments.at(i);
        if (dashdash || !a.startsWith(u'-') || a == u"-") {
            positional.append(a);
            continue;
        }
        if (a == u"--") {
            dashdash = true;
            continue;
        }
        if (global(i)) {
            if (inv.error)
                return inv;
            continue;
        }
        const Option option = splitOption(a);
        const auto arg = std::ranges::find_if(command->args, [&option](const Arg &candidate) {
            return !candidate.positional && optionName(candidate.name) == option.name;
        });
        if (arg == command->args.end()) {
            inv.error = usage(u"unknown_option"_s,
                              tr("Unknown option %1 for %2").arg(option.name, command->words().join(u' ')),
                              {{u"option"_s, option.name}});
            return inv;
        }
        if (arg->type == ArgType::Bool) {
            args.insert(arg->name, flagValue(option));
            continue;
        }
        const auto value = valueOf(i, option);
        if (!value)
            return inv;
        args.insert(arg->name, *value);
    }
    for (const Arg &a : command->args) {
        if (!a.positional || positional.isEmpty() || args.contains(a.name))
            continue;
        if (a.rest) {
            args.insert(a.name, positional.join(u' '));
            positional.clear();
        } else {
            args.insert(a.name, positional.takeFirst());
        }
    }
    if (!positional.isEmpty()) {
        inv.error = usage(u"extra_arguments"_s, tr("Too many arguments: %1").arg(positional.join(u' ')),
                          {{u"arguments"_s, QJsonArray::fromStringList(positional)}});
        return inv;
    }
    inv.error = checkGlobals(g);
    if (!inv.error)
        inv.args = args;
    return inv;
}

QJsonObject checkArguments(const Command &command, const QJsonObject &args, std::optional<Error> *error)
{
    QJsonObject out;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const auto arg = std::ranges::find(command.args, it.key(), &Arg::name);
        if (arg == command.args.end()) {
            *error = usage(u"unknown_argument"_s, tr("Unknown argument %1").arg(it.key()), {{u"argument"_s, it.key()}});
            return {};
        }
        const QJsonValue v = it.value();
        // `message`: "%1 must be …" with the argument's name for %1 (whole sentences, translated).
        const auto wrongType = [&](const QString &message) {
            *error = usage(u"bad_argument"_s, message.arg(it.key()), {{u"argument"_s, it.key()}});
            return QJsonObject();
        };
        switch (arg->type) {
        case ArgType::Bool:
            if (!v.isBool())
                return wrongType(tr("%1 must be true or false"));
            out.insert(arg->name, v);
            break;
        case ArgType::Int: {
            if (v.isDouble() && v.toDouble() == double(v.toInteger())) {
                out.insert(arg->name, v.toInteger());
                break;
            }
            bool ok = false;
            const qint64 n = v.toString().trimmed().toLongLong(&ok);
            if (!v.isString() || !ok)
                return wrongType(tr("%1 must be a whole number"));
            out.insert(arg->name, n);
            break;
        }
        case ArgType::Enum: {
            const QString value = v.toString().trimmed().toLower();
            if (!v.isString() || !arg->values.contains(value))
                return wrongType(tr("%1 must be one of: %2").arg(u"%1"_s, arg->values.join(u", "_s)));
            out.insert(arg->name, value);
            break;
        }
        default:
            if (!v.isString())
                return wrongType(tr("%1 must be a string"));
            out.insert(arg->name, v);
            break;
        }
    }
    for (const Arg &a : command.args) {
        if (a.required && !a.fromStdin && !out.contains(a.name)) {
            *error = usage(u"missing_argument"_s,
                           tr("Missing argument: %1").arg(a.positional ? a.name : optionName(a.name)),
                           {{u"argument"_s, a.name}});
            return {};
        }
    }
    return out;
}

} // namespace brulion::cli
