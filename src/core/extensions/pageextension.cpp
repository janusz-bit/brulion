#include "extensions/pageextension.h"

#include "extensions/notebookhost.h"
#include "text/text.h"

#include <QCryptographicHash>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

struct Fence
{
    QChar ch;
    qsizetype length = 0;
    QString lang;
};

/// A line that opens a fenced block: indentation, an optional list marker ("- ", "* ", "+ ",
/// "1. ", "1) " — Logseq writes code blocks inside list items), then ``` or ~~~ (three or more).
std::optional<Fence> openingFence(QStringView line)
{
    qsizetype i = 0;
    const auto skipBlanks = [&] {
        while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
            ++i;
    };
    skipBlanks();
    if (i + 1 < line.size() && (line[i] == u'-' || line[i] == u'*' || line[i] == u'+') && line[i + 1] == u' ') {
        i += 2;
        skipBlanks();
    } else {
        qsizetype d = i;
        while (d < line.size() && line[d].isDigit() && d - i < 9)
            ++d;
        if (d > i && d + 1 < line.size() && (line[d] == u'.' || line[d] == u')') && line[d + 1] == u' ') {
            i = d + 2;
            skipBlanks();
        }
    }
    if (i >= line.size() || (line[i] != u'`' && line[i] != u'~'))
        return std::nullopt;
    const QChar ch = line[i];
    qsizetype n = 0;
    while (i + n < line.size() && line[i + n] == ch)
        ++n;
    if (n < 3)
        return std::nullopt;
    const QStringView info = line.sliced(i + n).trimmed();
    if (ch == u'`' && info.contains(u'`'))
        return std::nullopt; // inline code, not a fence (CommonMark)
    qsizetype end = 0;
    while (end < info.size() && !info[end].isSpace())
        ++end;
    QString lang = info.first(end).toString().toLower();
    while (lang.startsWith(u'{') || lang.startsWith(u'.'))
        lang.remove(0, 1);
    if (lang.endsWith(u'}'))
        lang.chop(1);
    return Fence{ch, n, lang};
}

bool closesFence(QStringView line, const Fence &open)
{
    const QStringView t = line.trimmed();
    return t.size() >= open.length && std::ranges::all_of(t, [&](QChar c) { return c == open.ch; });
}

bool isCodeLanguage(const QString &lang) { return lang == u"luau" || lang == u"lua"; }

QStringList linesOf(QStringView content)
{
    QStringList out;
    for (QStringView line : content.split(u'\n')) {
        if (line.endsWith(u'\r'))
            line.chop(1);
        out << line.toString();
    }
    return out;
}

/// The first line after a leading frontmatter block (0 when there is none).
qsizetype afterFrontmatter(const QStringList &lines)
{
    if (lines.isEmpty() || lines.first().trimmed() != u"---")
        return 0;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t == u"---" || t == u"...")
            return i + 1;
    }
    return 0; // never closed: no frontmatter
}

/// A code block: its fence lines (close = -1 when it runs to the end) and its own lines.
struct CodeBlock
{
    qsizetype open = 0, close = -1;
    QStringList lines;
};

QList<CodeBlock> codeBlocks(const QStringList &lines)
{
    QList<CodeBlock> out;
    std::optional<Fence> fence;
    bool code = false;
    for (qsizetype i = afterFrontmatter(lines); i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (!fence) {
            fence = openingFence(line);
            if (fence) {
                code = isCodeLanguage(fence->lang);
                if (code)
                    out.append(CodeBlock{i, -1, {}});
            }
            continue;
        }
        if (closesFence(line, *fence)) {
            if (code)
                out.last().close = i;
            fence.reset();
            code = false;
            continue;
        }
        if (code)
            out.last().lines << line;
    }
    return out;
}

/// The words of a property value: a list or "a, b c", without brackets, '#' or quotes.
QStringList wordsOf(const QVariant &value)
{
    QStringList raw;
    if (value.metaType().id() == QMetaType::QStringList || value.metaType().id() == QMetaType::QVariantList)
        raw = value.toStringList();
    else
        raw << value.toString();
    QStringList out;
    static const QRegularExpression separators(u"[,;\\s]+"_s);
    for (const QString &item : std::as_const(raw)) {
        for (QString word : item.split(separators, Qt::SkipEmptyParts)) {
            word.remove(u'[').remove(u']').remove(u'"').remove(u'\'');
            while (word.startsWith(u'#'))
                word.remove(0, 1);
            if (!word.isEmpty())
                out << word;
        }
    }
    return out;
}

/// A page property by any of `keys` (case-insensitive): a `key:: value` line, else the frontmatter.
QVariant pageProperty(const ParsedNote &note, std::initializer_list<QStringView> keys)
{
    for (const QVariantMap *map : {&note.properties, &note.frontmatter}) {
        for (auto it = map->cbegin(); it != map->cend(); ++it) {
            for (QStringView key : keys)
                if (it.key().compare(key, Qt::CaseInsensitive) == 0)
                    return it.value();
        }
    }
    return {};
}

QString padded(const QStringList &lines, const QList<CodeBlock> &blocks)
{
    QStringList src(lines.size());
    for (const CodeBlock &b : blocks) {
        for (qsizetype k = 0; k < b.lines.size(); ++k)
            src[b.open + 1 + k] = b.lines[k];
    }
    return src.join(u'\n');
}

} // namespace

bool isExtensionPage(const ParsedNote &note)
{
    // note.tags has every tag of the page (blocks too): the cheap test first.
    if (std::ranges::none_of(extensionTags(), [&](const QString &t) { return note.tags.contains(t); }))
        return false;
    const QStringList tags = pageTags(note);
    return std::ranges::any_of(extensionTags(), [&](const QString &t) { return tags.contains(t); });
}

QString extensionId(const QString &title)
{
    const QString folded = text::foldKey(title);
    QString out;
    out.reserve(folded.size());
    for (const QChar c : folded) {
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9'))
            out += c;
        else if (!out.isEmpty() && !out.endsWith(u'-'))
            out += u'-';
    }
    while (out.endsWith(u'-'))
        out.chop(1);
    out = out.left(64);
    while (out.endsWith(u'-'))
        out.chop(1);
    return out.isEmpty() ? u"extension"_s : out;
}

ExtensionCode extensionCode(QStringView content)
{
    const QStringList lines = linesOf(content);
    const QList<CodeBlock> blocks = codeBlocks(lines);
    ExtensionCode out;
    out.source = padded(lines, blocks);
    QStringList parts;
    for (const CodeBlock &b : blocks) {
        parts << b.lines.join(u'\n');
        if (out.firstLine < 0 && !b.lines.isEmpty())
            out.firstLine = int(b.open + 1);
    }
    out.code = parts.join(u"\n\n"_s);
    out.blocks = int(blocks.size());
    out.unclosed = !blocks.isEmpty() && blocks.last().close < 0;
    return out;
}

QString codeBlockAt(QStringView content, int line)
{
    const QStringList lines = linesOf(content);
    for (const CodeBlock &b : codeBlocks(lines)) {
        const qsizetype end = b.close < 0 ? lines.size() - 1 : b.close;
        if (line >= b.open && line <= end)
            return padded(lines, {b});
    }
    return {};
}

QStringList permissionsFrom(const QStringList &words, QStringList *unknown)
{
    QStringList out;
    for (const QString &word : words) {
        const QString w = text::foldKey(word);
        QString canonical;
        if (w == u"write" || w == u"zapis" || w == u"pisanie")
            canonical = u"write"_s;
        else if (w == u"ai" || w == u"si")
            canonical = u"ai"_s;
        else if (w == u"read" || w == u"odczyt")
            continue; // reading is always allowed
        if (canonical.isEmpty()) {
            if (unknown)
                *unknown << word;
        } else if (!out.contains(canonical)) {
            out << canonical;
        }
    }
    out.sort();
    return out;
}

QString extensionHash(const QStringList &permissions, const QString &code)
{
    QStringList sorted = permissions;
    sorted.sort();
    const QByteArray data = "brulion-extension/1\n" + sorted.join(u',').toUtf8() + '\n' + code.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

Extension PageExtension::extension() const
{
    Extension e;
    e.id = id;
    e.source = source;
    e.permissions = permissions;
    e.name = title;
    return e;
}

PageExtension readPageExtension(const VaultPath &path, const ParsedNote &note, QStringView content)
{
    PageExtension out;
    out.path = path;
    out.title = note.title;
    out.id = extensionId(note.title);
    const ExtensionCode code = extensionCode(content);
    out.source = code.source;
    out.code = code.code;
    out.firstLine = code.firstLine;
    out.blocks = code.blocks;
    QStringList unknown;
    out.permissions = permissionsFrom(wordsOf(pageProperty(note, {u"permissions", u"uprawnienia"})), &unknown);
    out.description = pageProperty(note, {u"description", u"opis"}).toString().trimmed();
    const QStringList overrides = wordsOf(pageProperty(note, {u"overrides", u"zastępuje", u"zastepuje"}));
    out.overrides = overrides.isEmpty() ? QString() : extensionId(overrides.first());
    if (code.blocks == 0)
        out.problems << u"no ```luau block"_s;
    if (code.unclosed)
        out.problems << u"unclosed ```luau block"_s;
    for (const QString &word : std::as_const(unknown))
        out.problems << u"unknown permission: "_s + word;
    out.hash = extensionHash(out.permissions, out.code);
    return out;
}

QString newExtensionPage(const QString &language)
{
    if (language == u"en") {
        return uR"md(---
tags: [extension]
description: My first extension — a command, a view and a hook.
---
This page is an **extension**: the code in its `luau` blocks runs in Brulion. Change it and save —
Brulion loads it again at once; an error names the line of this page. The `b` API and more
examples: "Extensions" in Brulion's documentation (the palette: "Brulion Documentation").

A command in the palette (Ctrl+K): "Hello".

```luau
b.command("hello", function(ctx)
    return "Hello! Today is " .. b.today()
end, { label = "Hello" })
```

A view: the pages tagged #project (the palette: "Projects").

```luau
b.view("projects", function(params)
    local links = {}
    for _, name in b.pages({ tag = "project" }) do
        table.insert(links, b.ui.link(name))
    end
    if #links == 0 then
        return b.ui.empty("No projects", "Tag a page #project.")
    end
    return b.ui.column({ b.ui.heading("Projects"), b.ui.column(links) })
end, nil, { label = "Projects" })
```

A hook: every page saved here, in the Luau console (the palette: "Luau console").

```luau
b.on("save", function(event)
    print("Saved: " .. event.page)
end)
```
)md"_s;
    }
    return uR"md(---
tags: [rozszerzenie]
opis: Moje pierwsze rozszerzenie — polecenie, widok i reakcja na zapis.
---
Ta strona to **rozszerzenie**: kod w jej blokach `luau` działa w Brulionie. Zmień go i zapisz —
Brulion od razu wczyta go od nowa; błąd wskaże wiersz tej strony. API `b` i więcej przykładów:
„Rozszerzenia” w dokumentacji Brulionu (w palecie: „Dokumentacja Brulionu”).

Polecenie w palecie (Ctrl+K): „Powitanie”.

```luau
b.command("powitanie", function(ctx)
    return "Dzień dobry! Dziś jest " .. b.today()
end, { label = "Powitanie" })
```

Widok: strony z tagiem #projekt (w palecie: „Projekty”).

```luau
b.view("projekty", function(params)
    local linki = {}
    for _, nazwa in b.pages({ tag = "projekt" }) do
        table.insert(linki, b.ui.link(nazwa))
    end
    if #linki == 0 then
        return b.ui.empty("Brak projektów", "Dodaj do strony tag #projekt.")
    end
    return b.ui.column({ b.ui.heading("Projekty"), b.ui.column(linki) })
end, nil, { label = "Projekty" })
```

Reakcja na zapis: każda zapisana tu strona w konsoli Luau (w palecie: „Konsola Luau”).

```luau
b.on("save", function(zdarzenie)
    print("Zapisano: " .. zdarzenie.page)
end)
```
)md"_s;
}

QString builtinSourcePage(const Extension &builtin, const QString &language)
{
    const bool en = language == u"en";
    const QString &source = builtin.source;
    QStringList perms;
    for (const QString &p : builtin.permissions)
        perms << (en ? p : p == u"write" ? u"zapis"_s : p);
    QString out = u"---\n"_s;
    out += en ? u"tags: [extension]\n"_s : u"tags: [rozszerzenie]\n"_s;
    out += (en ? u"overrides: "_s : u"zastępuje: "_s) + builtin.id + u'\n';
    if (!perms.isEmpty())
        out += (en ? u"permissions: ["_s : u"uprawnienia: ["_s) + perms.join(u", "_s) + u"]\n"_s;
    out += en ? u"description: The source of a built-in extension, run from this page.\n"_s
              : u"opis: Kod wbudowanego rozszerzenia, uruchamiany z tej strony.\n"_s;
    out += u"---\n"_s;
    out += en ? u"This page is the **source of a built-in extension**. While it exists and is approved on this device, "
                "the extension runs from this code instead of the built-in one: change it and save, and the change "
                "works at once. Delete the page to go back to the built-in version (Settings → Extensions).\n"_s
              : u"Ta strona to **kod wbudowanego rozszerzenia**. Dopóki istnieje i jest zatwierdzona na tym urządzeniu, "
                "rozszerzenie działa z tego kodu zamiast z wbudowanego: zmień go i zapisz, a zmiana zadziała od razu. "
                "Usuń stronę, żeby wrócić do wersji wbudowanej (Ustawienia → Rozszerzenia).\n"_s;
    // Sections: "-- Parsing ------------" lines (a title, then dashes to the margin).
    static const QRegularExpression section(u"^-- ([^-].*?) -{8,}\\s*$"_s);
    const QString fence = source.contains(u"```"_s) ? u"~~~~"_s : u"```"_s;
    QStringList lines = linesOf(source);
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    QString title = en ? u"Start"_s : u"Początek"_s;
    QStringList chunk;
    const auto flush = [&] {
        while (!chunk.isEmpty() && chunk.last().trimmed().isEmpty())
            chunk.removeLast();
        if (chunk.isEmpty())
            return;
        out += u"\n## "_s + title + u"\n\n"_s + fence + u"luau\n"_s + chunk.join(u'\n') + u'\n' + fence + u'\n';
        chunk.clear();
    };
    for (const QString &line : std::as_const(lines)) {
        if (const QRegularExpressionMatch m = section.match(line); m.hasMatch()) {
            flush();
            title = m.captured(1).trimmed();
            continue;
        }
        if (chunk.isEmpty() && line.trimmed().isEmpty())
            continue;
        chunk << line;
    }
    flush();
    return out;
}

} // namespace brulion::extensions
