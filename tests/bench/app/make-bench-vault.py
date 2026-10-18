#!/usr/bin/env python3
"""A big synthetic notebook for the app benchmarks (tests/bench/app/README.md).

  tests/bench/app/make-bench-vault.py <dir> [--notes 10000] [--days 730] [--today 2026-10-14]
                                            [--seed 7] [--big-blocks 5000]

Writes `--notes` Markdown notes in all:
  * journals/YYYY-MM-DD.md — `--days` days up to `--today` (about one day in ten skipped, as in
    real use), Logseq-like outlines from one line to long days: wrapping paragraphs, nested lists,
    tasks with 📅 dates and ⏰ reminders, links and tags, now and then a code block or a table;
  * pages in ~40 folders (outlines and documents, 3–60 blocks, ~1.3 KB on average) that link to
    each other ([[…]]), carry tags (#…), tasks with dates, a few ```query blocks and frontmatter;
  * Bench/Konspekt 5000.md — an outline of `--big-blocks` blocks (nested up to 3 levels);
    Bench/Dokument 5000.md — a document (`brulion: document`) of `--big-blocks` blocks
    (headings, paragraphs, lists); Bench/Średnia strona.md (300 blocks), Bench/Mała strona.md
    (12 blocks) — the pages the typing benchmarks edit.
Deterministic for a seed. Prints a JSON summary (counts, bytes).
"""
import argparse
import datetime as dt
import json
import os
import random

WORDS = (
    "rano spotkanie z zespołem omówiliśmy plan na kolejny tydzień zażółć gęślą jaźń "
    "trzeba jeszcze zadzwonić do hydraulika i zamówić farbę do łazienki po południu "
    "spacer nad rzeką wieczorem czytanie książki o historii Krakowa śnieg przyszedł "
    "wcześniej niż zwykle a w ogrodzie zostały jeszcze jabłka do zebrania pomysł na "
    "artykuł o synchronizacji notatek między telefonem i laptopem kawa źródło ćwiczenia "
    "rower naprawa hamulców zakupy na targu żurek pierogi łąka gwiazdy pociąg do Gdańska "
    "budżet faktura umowa klient termin wdrożenie serwer kopia zapasowa przegląd kodu "
    "lista zakupów mleko chleb masło jajka pomidory ogórki ser wędlina sok jabłkowy"
).split()
FOLDERS = [
    "", "Projekty", "Projekty/Dom", "Projekty/Ogród", "Projekty/Brulion", "Projekty/Łódź",
    "Praca", "Praca/Spotkania", "Praca/Klienci", "Praca/Wdrożenia", "Praca/Notatki",
    "Książki", "Książki/2025", "Książki/2026", "Przepisy", "Przepisy/Zupy", "Przepisy/Ciasta",
    "Zdrowie", "Podróże", "Podróże/Bieszczady", "Podróże/Gdańsk", "Nauka", "Nauka/Historia",
    "Nauka/Fizyka", "Nauka/Języki", "Finanse", "Finanse/Rachunki", "Ludzie", "Pomysły",
    "Archiwum", "Archiwum/2023", "Archiwum/2024", "Archiwum/2025", "Hobby", "Hobby/Rower",
    "Hobby/Fotografia", "Dom", "Dom/Naprawy", "Szkoła", "Szkoła/Matematyka",
]
TOPICS = [
    "Spotkanie", "Notatka", "Plan", "Przegląd", "Pomysł", "Przepis", "Lista", "Raport",
    "Wycieczka", "Książka", "Kurs", "Projekt", "Zadania", "Budżet", "Rozmowa", "Szkic",
]
TAGS = ["#praca", "#dom", "#pomysł", "#zdrowie", "#książki", "#ogród", "#źródła", "#projekt",
        "#klient", "#ważne", "#później", "#zakupy", "#rower", "#nauka", "#finanse", "#podróże",
        "#przepis", "#spotkanie", "#łódź", "#kraków"] + ["#temat%02d" % i for i in range(30)]


def title_case(text):
    return text[0].upper() + text[1:] if text else text


class Gen:
    def __init__(self, seed, today):
        self.rng = random.Random(seed)
        self.today = today
        self.titles = []

    def sentence(self, lo=5, hi=18, links=True):
        rng = self.rng
        words = [rng.choice(WORDS) for _ in range(rng.randint(lo, hi))]
        if links and self.titles and rng.random() < 0.3:
            words.insert(rng.randrange(len(words)), "[[%s]]" % rng.choice(self.titles))
        if rng.random() < 0.25:
            words.append(rng.choice(TAGS))
        if rng.random() < 0.08:
            words.insert(rng.randrange(len(words)), "**%s**" % rng.choice(WORDS))
        return title_case(" ".join(words))

    def future(self, base, lo=-30, hi=60):
        return (base + dt.timedelta(days=self.rng.randint(lo, hi))).isoformat()

    def task(self, base, indent=""):
        rng = self.rng
        r = rng.random()
        if r < 0.35:
            return "%s- [ ] %s 📅 %s" % (indent, self.sentence(3, 8), self.future(base))
        if r < 0.45:
            return "%s- [ ] %s ⏰ %s %02d:%02d" % (indent, self.sentence(3, 7), self.future(base, 1, 40),
                                                rng.randint(7, 21), rng.choice([0, 15, 30, 45]))
        if r < 0.75:
            return "%s- [x] %s" % (indent, self.sentence(3, 8))
        return "%s- [ ] %s" % (indent, self.sentence(3, 8))

    def outline(self, base, blocks):
        """An outline of about `blocks` blocks (Logseq-like)."""
        rng = self.rng
        lines = []
        n = 0
        while n < blocks:
            r = rng.random()
            if r < 0.12:
                lines.append(self.task(base))
                n += 1
            elif r < 0.22:
                lines.append("- " + " ".join(self.sentence(10, 25) + "." for _ in range(rng.randint(2, 4))))
                n += 1
            elif r < 0.40:
                lines.append("- %s" % self.sentence())
                n += 1
                for _ in range(rng.randint(1, 4)):
                    if n >= blocks:
                        break
                    if rng.random() < 0.15:
                        lines.append(self.task(base, "  "))
                    else:
                        lines.append("  - %s" % self.sentence(3, 12))
                    n += 1
                    if rng.random() < 0.3 and n < blocks:
                        lines.append("    - %s" % self.sentence(3, 10))
                        n += 1
            elif r < 0.42:
                lines.append("- > %s" % self.sentence(8, 20))
                n += 1
            elif r < 0.43:
                lines.append("- ```python")
                lines.append("  def suma(a, b):")
                lines.append("      return a + b  # %s" % rng.choice(WORDS))
                lines.append("  ```")
                n += 1
            elif r < 0.44:
                lines.append("- | Rzecz | Ilość |")
                lines.append("  |---|---:|")
                for _ in range(rng.randint(2, 4)):
                    lines.append("  | %s | %d |" % (rng.choice(WORDS), rng.randint(1, 9)))
                n += 1
            elif r < 0.46:
                lines.append("- ## %s" % self.sentence(2, 5, links=False))
                n += 1
            else:
                lines.append("- %s" % self.sentence())
                n += 1
        return lines

    def document(self, base, blocks, title):
        """A document (Obsidian-like) of about `blocks` blocks."""
        rng = self.rng
        lines = ["---", "brulion: document", "tags: [%s]" % rng.choice(TAGS)[1:], "---", "# %s" % title, ""]
        n = 1
        while n < blocks:
            r = rng.random()
            if r < 0.08:
                lines += ["## %s" % self.sentence(2, 6, links=False), ""]
                n += 1
            elif r < 0.55:
                lines += [" ".join(self.sentence(8, 22) + "." for _ in range(rng.randint(1, 4))), ""]
                n += 1
            elif r < 0.80:
                for _ in range(rng.randint(2, 5)):
                    if n >= blocks:
                        break
                    lines.append("- %s" % self.sentence(3, 12))
                    n += 1
                lines.append("")
            elif r < 0.90:
                for _ in range(rng.randint(1, 3)):
                    if n >= blocks:
                        break
                    lines.append(self.task(base))
                    n += 1
                lines.append("")
            elif r < 0.95:
                lines += ["> %s" % self.sentence(8, 20), ""]
                n += 1
            else:
                for i in range(rng.randint(2, 4)):
                    if n >= blocks:
                        break
                    lines.append("%d. %s" % (i + 1, self.sentence(3, 10)))
                    n += 1
                lines.append("")
        return lines


def write(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = "\n".join(lines) + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(data)
    return len(data.encode("utf-8"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--notes", type=int, default=10000)
    ap.add_argument("--days", type=int, default=730)
    ap.add_argument("--today", default="2026-10-14")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--big-blocks", type=int, default=5000)
    args = ap.parse_args()
    today = dt.date.fromisoformat(args.today)
    g = Gen(args.seed, today)
    rng = g.rng
    root = args.dir
    os.makedirs(root, exist_ok=True)
    total_bytes = 0

    # Titles first, so that notes can link to any page.
    special = 4
    days = [today - dt.timedelta(days=i) for i in range(args.days)]
    days = [d for i, d in enumerate(days) if i == 0 or rng.random() >= 0.1]
    n_pages = max(0, args.notes - len(days) - special)
    pages = []
    used = set()
    for i in range(n_pages):
        folder = FOLDERS[i % len(FOLDERS)] if rng.random() < 0.9 else ""
        while True:
            title = "%s %s %s" % (rng.choice(TOPICS), rng.choice(WORDS), rng.choice(WORDS))
            title = title_case(title)
            if rng.random() < 0.5:
                title += " %d" % rng.randint(1, 999)
            key = title.casefold()
            if key not in used:
                used.add(key)
                break
        pages.append((folder, title))
    g.titles = [t for _, t in pages[:2000]] + ["Konspekt 5000", "Dokument 5000"]

    for d in days:
        lines = g.outline(d, rng.choice([1, 2, 3, 4, 6, 8, 10, 14, 20, 30]))
        total_bytes += write(os.path.join(root, "journals", d.isoformat() + ".md"), lines)

    stats = {"journals": len(days), "pages": 0, "documents": 0, "outlines": 0, "queries": 0}
    for i, (folder, title) in enumerate(pages):
        base = today - dt.timedelta(days=rng.randint(0, 700))
        size = int(min(60, max(3, rng.lognormvariate(2.4, 0.7))))
        if rng.random() < 0.4:
            lines = g.document(base, size, title)
            stats["documents"] += 1
        else:
            lines = []
            if rng.random() < 0.2:
                lines += ["---", "tags: [%s, %s]" % (rng.choice(TAGS)[1:], rng.choice(TAGS)[1:]),
                          "status: %s" % rng.choice(["szkic", "gotowe", "w toku"]), "---"]
            lines += g.outline(base, size)
            stats["outlines"] += 1
        if i % 200 == 7:
            lines += ["- Otwarte zadania z tagiem:", "  ```query", "  task:open tag:%s" % rng.choice(TAGS)[1:], "  ```"]
            stats["queries"] += 1
        total_bytes += write(os.path.join(root, folder, title + ".md"), lines)
        stats["pages"] += 1

    bench = os.path.join(root, "Bench")
    # The typing benchmarks edit row 10: the first 20 blocks are plain text.
    plain = ["Akapit startowy %d do pisania: %s" % (i + 1, g.sentence(6, 14, links=False)) for i in range(20)]
    outline = ["- " + t for t in plain] + g.outline(today, args.big_blocks - 20)
    total_bytes += write(os.path.join(bench, "Konspekt 5000.md"), outline)
    doc = g.document(today, args.big_blocks - 20, "Dokument 5000")
    head = doc.index("# Dokument 5000") + 2
    doc[head:head] = sum(([t, ""] for t in plain), [])
    total_bytes += write(os.path.join(bench, "Dokument 5000.md"), doc)
    total_bytes += write(os.path.join(bench, "Średnia strona.md"), ["- " + t for t in plain] + g.outline(today, 280))
    small = ["- Mała strona do pisania", "- Druga linia z [[Konspekt 5000]]", "- [ ] Zadanie 📅 %s" % today.isoformat()]
    small += ["- %s" % g.sentence(4, 10, links=False) for _ in range(9)]
    total_bytes += write(os.path.join(bench, "Mała strona.md"), small)
    # 50 pages for the page-switching benchmarks (a deterministic sample), next to the vault.
    sample = random.Random(args.seed + 1).sample(pages, min(50, len(pages)))
    with open(os.path.normpath(root) + ".pages.txt", "w", encoding="utf-8") as f:
        for folder, title in sample:
            f.write((folder + "/" if folder else "") + title + ".md\n")
    stats["special"] = special
    stats["notes"] = stats["journals"] + stats["pages"] + special
    stats["bytes"] = total_bytes
    print(json.dumps(stats))


if __name__ == "__main__":
    main()
