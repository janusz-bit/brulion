// A frozen corpus of notes for golden digests of the parser's output (markdown::ParserVersion) and
// of the index cache's format: hand-written edge cases (line endings, BOM, NUL, lone surrogates,
// YAML, Logseq properties and ids, callouts, tables, fences, tasks with every metadata token,
// reminders, NFD names, comments, formulas) and deterministic combinations of note fragments.
// NEVER edit the existing notes: the digests depend on every character. Add new notes at the end
// (and update the digests) only together with a version bump that needs them. Header-only.
#pragma once

#include "types.h"

#include <QList>
#include <QString>

#include <utility>

namespace brulion::testing {

inline QList<std::pair<VaultPath, QString>> noteCorpus()
{
    QList<std::pair<VaultPath, QString>> out;
    auto add = [&](const char16_t *path, const QString &content) { out.append({QString::fromUtf16(path), content}); };

    add(u"Pusta.md", QString());
    add(u"Tylko frontmatter.md", QStringLiteral("---\n---"));
    add(u"BOM.md",
        QString(QChar(0xfeff)) + QStringLiteral("---\ntitle: Z BOM-em\ntags: [a, b]\n---\n# Nagłówek\n- punkt #tag\n"));
    add(u"CRLF.md",
        QStringLiteral("# Zakupy\r\n\r\n- [ ] Mleko 📅 2026-10-20\r\n  - dziecko\r\n- [x] Chleb ✅ 2026-10-19\r\n"));
    add(u"CR.md", QStringLiteral("- pierwszy\r- drugi #cr\r  ciąg dalszy\r"));
    add(u"Mieszane.md", QStringLiteral("- a\n- b\r\n- c\r- d"));
    add(u"NUL.md", QStringLiteral("- tekst z ") + QChar(0) + QStringLiteral(" znakiem NUL\n"));
    add(u"Surogat.md", QStringLiteral("- samotny ") + QChar(0xd800) + QStringLiteral(" surogat i para 😀\n"));
    add(u"YAML.md",
        QStringLiteral("---\n"
                                   "mapa:\n  zagnieżdżona:\n    klucz: wartość\n  lista:\n    - 1\n    - dwa\n"
                                   "pusto: null\nprawda: True\nliczba: -12\nwykładnik: 3.5e2\n"
                                   "cytat: \"cytat: z dwukropkiem\"\nlink: \"[[Łódź]]\"\naliases: [Alias Jeden, \"Alias, Dwa\"]\n"
                                   "tags:\n  - projekt\n  - Dom/Remont\n---\nTreść po YAML.\n"));
    add(u"Logseq/Książki.md",
        QStringLiteral("alias:: Lektury, [[Czytanie]]\n"
                                             "tags:: Książki, fantastyka\n\n"
                                             "- Solaris\n  id:: 6523a1b2-0c3d-4e5f-8a9b-0123456789ab\n  collapsed:: true\n"
                                             "  - autor:: Lem\n    rok:: 1961\n"
                                             "- TODO Przeczytać Diunę\n  SCHEDULED: <2026-10-20 Tue>\n  DEADLINE: <2026-11-01>\n"
                                             "- Odwołanie ((6523a1b2-0c3d-4e5f-8a9b-0123456789ab)) i {{embed ((6523a1b2-0c3d-4e5f-8a9b-0123456789ab))}}\n"
                                             "- DONE Zrobione\n- LATER Później\n- NOW Teraz\n"));
    {
        QString nested = QStringLiteral("- lista\n");
        QString prefix;
        for (int i = 0; i < 16; ++i) {
            prefix += QStringLiteral("> ");
            nested += prefix
                      + QStringLiteral("[!note] Poziom %1\n").arg(i) + prefix
                                       + QStringLiteral("treść %1 #głęboko\n").arg(i);
        }
        add(u"Callouty.md", nested);
    }
    add(u"Tabela.md", QStringLiteral("| Nazwa | Cena |\n|---|---:|\n| Łopata | 30 zł |\n| Suma | =sum(B2:B2) |\n^tabela\n\nZobacz [[#^tabela]].\n"));
    add(u"Płot.md", QStringLiteral("- przed\n```python\nprint('niezamknięty')\n- nie lista\n"));
    add(u"Setext.md", QStringLiteral("Tytuł\n=====\n\nPodtytuł\n--------\n\nAkapit.\n"));
    add(u"Nieciągłe.md", QStringLiteral("- rodzic\n  - dziecko\n  ciąg dalszy rodzica\n- następny\n"));
    add(u"Zadania.md",
        QStringLiteral("- [/] Zażółć 📅 2026-10-20 ⏳ 2026-10-18 🛫 2026-10-17 ➕ 2026-10-01 ✅ 2026-10-21 🔁 every week ⏫\n"
                                      "- [-] Anulowane ❌ 2026-10-02 🔽\n"
                                      "- [>] Odłożone ⏬ #później\n"
                                      "- [ ] Z przypomnieniem ⏰ 2026-10-21 08:30\n"
                                      "- [ ] Alarm ⏰! jutro\n"
                                      "- ⏰ 2026-10-22 bez zadania 🔁 every day\n"
                                      "1. [ ] numerowane 🔼\n"));
    add(u"Z\u0307o\u0301\u0142w.md",
        QStringLiteral("- NFD w nazwie pliku [[Żółw]] #Żółw i [[Z\u0307o\u0301\u0142w]]\n"));
    add(u"Długa linia.md", QString(QStringLiteral("słowo ")).repeated(20000) + QStringLiteral("#koniec\n"));
    add(u"Komentarze.md",
        QStringLiteral("Akapit %%ukryty\nkomentarz%% widoczny `%%kod%%` [[Link %%nie%%]]\n\n%%\n- [ ] ukryte zadanie\n%%\n"));
    add(u"Wzory.md", QStringLiteral("$$\n\\frac{a}{b} [[nie link]] #nietag\n$$\n\nTekst $x^2$ i $$y$$ w linii #tag\n\n- $$\n  z = 1\n  $$\n"));
    add(u"Linki.md", QStringLiteral("[[Strona]] [[Strona|alias]] [[Strona#Nagłówek]] [[Strona#^blok]] ![[obraz.png]] "
                                    "[md](Inna%20strona.md) <https://example.com> https://przykład.pl [C#](C%23.md) "
                                    "![[Notatka#^id|osadzenie]] ((abc123)) [[ ]] [[]]\n"));
    add(u"Właściwości.md", QStringLiteral("właściciel:: Łucja\nTyp:: #projekt\nTags:: a, b\ntags:: c\n\n"
                                          "- blok\n  klucz:: wartość\n  Klucz:: druga\n  pusty::\n"));
    add(u"journals/2026-10-28.md", QStringLiteral("- dziennik ⏰ 21:30\n- [[Plan]] #dziennik\n"));
    add(u"Dziennik/2026-10-28 środa.md", QStringLiteral("- inny folder dziennika\n"));
    add(u"HTML.md", QStringLiteral("<div>\n[[nie link]] #nie\n</div>\n\n<details><summary>x</summary>\n\n- w środku #tak\n\n</details>\n"));
    add(u"Id.md",
        QStringLiteral("Akapit ^akapit\n\n- punkt ^punkt\n- duplikat ^punkt\n- id:: logseq-id\n\n```\nkod\n```\n^kod\n"));
    add(u"Liczby.md",
        QStringLiteral("```calc\ncena = 12,50 zł\nsuma = cena * 2\n```\n\n```query\ntask:todo tag:dom\n```\n"));
    add(u"Pusty blok.md", QStringLiteral("-\n- \n-   \n  -\n"));
    add(u"Tylko spacje.md", QStringLiteral("   \n\t\n"));
    add(u"Frontmatter niezamknięty.md", QStringLiteral("---\ntitle: brak końca\n- punkt\n"));

    // Deterministic combinations of fragments (no random generator: the same on every run).
    const QStringList fragments = {
            QStringLiteral("# Nagłówek %1\n"),
                    QStringLiteral("- punkt %1 z [[Strona %1]] i #tag%1\n"),
                            QStringLiteral("  - dziecko %1 ((id-%1))\n"),
                                    QStringLiteral("- [ ] zadanie %1 📅 2026-11-%1 ⏫\n"),
                                            QStringLiteral("- [x] zrobione %1 ✅ 2026-10-%1\n"),
                                                    QStringLiteral("Akapit %1 z **pogrubieniem** i `kodem`. ^id-%1\n\n"),
                                                            QStringLiteral("> [!tip] Wskazówka %1\n> treść\n\n"),
                                                                    QStringLiteral("- właściwość:: %1\n  collapsed:: true\n"),
                                                                            QStringLiteral("```js\nconst x = %1;\n```\n"),
                                                                                    QStringLiteral("| a | %1 |\n|---|---|\n| b | c |\n"),
                                                                                            QStringLiteral("- przypomnienie ⏰ 2026-12-%1 07:15\n"),
                                                                                                    QStringLiteral("---\n"),
                                                                                                            QStringLiteral("\r\n"),
                                                                                                                    QStringLiteral("    wcięty kod %1\n"),
                                                                                                            };
    for (int n = 0; n < 60; ++n) {
        QString content;
        for (int k = 0; k < 6 + n % 7; ++k)
            content += fragments[(n * 7 + k * 5 + k * k) % fragments.size()].arg(1 + (n + k) % 28);
        out.append({QStringLiteral("Generowane/Notatka %1.md").arg(n), content});
    }
    return out;
}

} // namespace brulion::testing
