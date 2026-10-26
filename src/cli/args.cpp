// The command line: global options, the command's words, its arguments (docs/CLI.md "Usage").
#include "cli_p.h"

#include <QHash>

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

/// The words of every command id: {"page": {"page.read", …}, …}.
bool isCommandPrefix(const QStringList &words)
{
    const QString prefix = words.join(u'.');
    for (const Command &c : commands()) {
        if (c.id == prefix || c.id.startsWith(prefix + u'.'))
            return true;
    }
    return false;
}

/// Commands whose first word alone means a subcommand ("journal" = "journal read").
const QHash<QString, QString> &defaultSubcommands()
{
    static const QHash<QString, QString> table{{u"journal"_s, u"journal.read"_s}, {u"app"_s, u"app.open"_s}};
    return table;
}

} // namespace

Invocation parseArguments(const QStringList &arguments)
{
    Invocation inv;
    Globals &g = inv.globals;
    const qsizetype n = arguments.size();
    // A global option at position i: true when handled (i advanced past its value).
    const auto global = [&](qsizetype &i) -> bool {
        const QString a = arguments.at(i);
        QString name = a;
        std::optional<QString> value;
        if (const qsizetype eq = a.indexOf(u'='); eq > 0 && a.startsWith(u"--")) {
            name = a.left(eq);
            value = a.mid(eq + 1);
        }
        if (QString *field = globalValue(g, name)) {
            if (!value) {
                if (i + 1 >= n) {
                    inv.error = usage(u"missing_value"_s, tr("%1 needs a value").arg(name), {{u"option"_s, name}});
                    return true;
                }
                value = arguments.at(++i);
            }
            *field = *value;
            return true;
        }
        if (bool *flag = globalFlag(g, name)) {
            *flag = !value || *value == u"true" || *value == u"1";
            return true;
        }
        return false;
    };

    // The command's words come first (after global options): the longest run naming a command.
    QStringList commandWords;
    qsizetype i = 0;
    for (; i < n && !inv.error; ++i) {
        const QString a = arguments.at(i);
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
    const Command *command = nullptr;
    qsizetype used = 0;
    for (qsizetype k = commandWords.size(); k >= 1 && !command; --k) {
        if (const Command *c = findCommand(commandWords.mid(0, k).join(u'.'))) {
            command = c;
            used = k;
        }
    }
    if (!command && !commandWords.isEmpty() && defaultSubcommands().contains(commandWords.first())) {
        command = findCommand(defaultSubcommands().value(commandWords.first()));
        used = 1;
    }
    // Words the command did not take are its first positional arguments.
    QStringList positional = commandWords.mid(used);
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

    // Its arguments: options (anywhere), positional words in order, global options too.
    QJsonObject args;
    bool dashdash = false;
    for (; i < n; ++i) {
        const QString a = arguments.at(i);
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
        QString name = a;
        std::optional<QString> value;
        if (const qsizetype eq = a.indexOf(u'='); eq > 0 && a.startsWith(u"--")) {
            name = a.left(eq);
            value = a.mid(eq + 1);
        }
        const Arg *arg = nullptr;
        for (const Arg &candidate : command->args) {
            if (!candidate.positional && optionName(candidate.name) == name)
                arg = &candidate;
        }
        if (!arg) {
            inv.error =
                    usage(u"unknown_option"_s, tr("Unknown option %1 for %2").arg(name, command->words().join(u' ')),
                          {{u"option"_s, name}});
            return inv;
        }
        if (arg->type == ArgType::Bool) {
            args.insert(arg->name, !value || *value == u"true" || *value == u"1");
            continue;
        }
        if (!value) {
            if (i + 1 >= n) {
                inv.error = usage(u"missing_value"_s, tr("%1 needs a value").arg(name), {{u"option"_s, name}});
                return inv;
            }
            value = arguments.at(++i);
        }
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
    if (g.format != u"json" && g.format != u"text") {
        inv.error = usage(u"bad_format"_s, tr("--format is json or text"), {{u"format"_s, g.format}});
        return inv;
    }
    if (!g.lang.isEmpty() && g.lang != u"pl" && g.lang != u"en") {
        inv.error = usage(u"bad_lang"_s, tr("--lang is pl or en"), {{u"lang"_s, g.lang}});
        return inv;
    }
    if (!g.now.isEmpty() && !QDateTime::fromString(g.now, u"yyyy-MM-dd HH:mm"_s).isValid()) {
        inv.error = usage(u"bad_now"_s, tr("--now takes \"YYYY-MM-DD HH:mm\" (local time)"), {{u"now"_s, g.now}});
        return inv;
    }
    inv.args = args;
    return inv;
}

QJsonObject checkArguments(const Command &command, const QJsonObject &args, std::optional<Error> *error)
{
    QJsonObject out;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const Arg *arg = nullptr;
        for (const Arg &a : command.args) {
            if (a.name == it.key())
                arg = &a;
        }
        if (!arg) {
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
            if (!v.isString() || !arg->values.contains(v.toString().trimmed().toLower()))
                return wrongType(tr("%1 must be one of: %2").arg(u"%1"_s, arg->values.join(u", "_s)));
            out.insert(arg->name, v.toString().trimmed().toLower());
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
