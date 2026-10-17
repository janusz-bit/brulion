#!/usr/bin/env python3
"""A big synthetic notebook for scroll measurements (tests/app/perf/scroll-probe.sh).

  tests/app/perf/make-vault.py <dir> [--days 365] [--today 2026-10-14] [--seed 1]

Writes <dir>/journals/YYYY-MM-DD.md for `--days` days up to `--today` (some days skipped, as in
real use), with day sizes from one line to long days: paragraphs that wrap, nested lists,
tasks with dates and reminders, links and tags, now and then a heading, a quote, a code block,
a table or a picture. Also "Długi konspekt.md" (an outline of ~400 blocks), "Długi dokument.md"
(a document of ~300 blocks) and the pages the days link to. Deterministic for a seed.
"""
import argparse
import datetime as dt
import os
import random

WORDS = (
    "rano spotkanie z zespołem omówiliśmy plan na kolejny tydzień zażółć gęślą jaźń "
    "trzeba jeszcze zadzwonić do hydraulika i zamówić farbę do łazienki po południu "
    "spacer nad rzeką wieczorem czytanie książki o historii Krakowa śnieg przyszedł "
    "wcześniej niż zwykle a w ogrodzie zostały jeszcze jabłka do zebrania pomysł na "
    "artykuł o synchronizacji notatek między telefonem i laptopem kawa źródło ćwiczenia "
    "rower naprawa hamulców zakupy na targu żurek pierogi łąka gwiazdy pociąg do Gdańska"
).split()
PAGES = ["Remont mieszkania", "Książki 2026", "Przepisy", "Projekt Łódź", "Ogród", "Zdrowie", "Praca"]
TAGS = ["#praca", "#dom", "#pomysł", "#zdrowie", "#książki", "#ogród", "#źródła"]


def sentence(rng, lo=5, hi=18):
    words = [rng.choice(WORDS) for _ in range(rng.randint(lo, hi))]
    if rng.random() < 0.3:
        words.insert(rng.randrange(len(words)), "[[%s]]" % rng.choice(PAGES))
    if rng.random() < 0.25:
        words.append(rng.choice(TAGS))
    text = " ".join(words)
    return text[0].upper() + text[1:]


def day_blocks(rng, date):
    """Markdown of one day (an outline, like Logseq journals)."""
    lines = []
    size = rng.choice([1, 2, 3, 4, 6, 8, 10, 14, 20, 30])
    if rng.random() < 0.3:
        lines.append("## Plan dnia")
    for _ in range(size):
        r = rng.random()
        future = (date + dt.timedelta(days=rng.randint(1, 20))).isoformat()
        if r < 0.15:
            lines.append("- [ ] %s 📅 %s" % (sentence(rng, 3, 8), future))
        elif r < 0.22:
            lines.append("- [x] %s" % sentence(rng, 3, 8))
        elif r < 0.27:
            lines.append("- %s ⏰ %s %02d:%02d" % (sentence(rng, 3, 7), future, rng.randint(7, 21), rng.choice([0, 15, 30, 45])))
        elif r < 0.37:
            # A long paragraph: wraps over several lines.
            lines.append("- " + " ".join(sentence(rng, 10, 25) + "." for _ in range(rng.randint(2, 5))))
        elif r < 0.45:
            lines.append("- %s" % sentence(rng))
            for _ in range(rng.randint(1, 4)):
                lines.append("  - %s" % sentence(rng, 3, 12))
                if rng.random() < 0.3:
                    lines.append("    - %s" % sentence(rng, 3, 10))
        elif r < 0.48:
            lines.append("- > %s" % sentence(rng, 8, 20))
        elif r < 0.50:
            lines.append("- ```python")
            lines.append("  def suma(a, b):")
            lines.append("      return a + b  # %s" % rng.choice(WORDS))
            lines.append("  ```")
        elif r < 0.52:
            lines.append("- | Rzecz | Ilość |")
            lines.append("  |---|---:|")
            for _ in range(rng.randint(2, 4)):
                lines.append("  | %s | %d |" % (rng.choice(WORDS), rng.randint(1, 9)))
        elif r < 0.535:
            lines.append("- Szkic: ![[assets/plan.svg]]")
        else:
            lines.append("- %s" % sentence(rng))
    return "\n".join(lines) + "\n"


SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="640" height="360" viewBox="0 0 640 360">
<rect width="640" height="360" fill="#fbf7ee"/><rect x="40" y="40" width="260" height="280" fill="none" stroke="#1f2430" stroke-width="4"/>
<rect x="300" y="40" width="300" height="160" fill="none" stroke="#1f2430" stroke-width="4"/>
<text x="120" y="190" font-size="28" fill="#e2574c">Salon</text><text x="390" y="130" font-size="28" fill="#e2574c">Kuchnia</text></svg>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--days", type=int, default=365)
    ap.add_argument("--today", default="2026-10-14")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    today = dt.date.fromisoformat(args.today)
    os.makedirs(os.path.join(args.dir, "journals"), exist_ok=True)
    os.makedirs(os.path.join(args.dir, "assets"), exist_ok=True)
    with open(os.path.join(args.dir, "assets", "plan.svg"), "w", encoding="utf-8") as f:
        f.write(SVG)
    written = 0
    for i in range(args.days):
        date = today - dt.timedelta(days=i)
        if i > 0 and rng.random() < 0.15:
            continue  # days without a note
        with open(os.path.join(args.dir, "journals", date.isoformat() + ".md"), "w", encoding="utf-8") as f:
            f.write(day_blocks(rng, date))
        written += 1
    for page in PAGES:
        with open(os.path.join(args.dir, page + ".md"), "w", encoding="utf-8") as f:
            f.write("- %s\n- %s\n" % (sentence(rng), sentence(rng)))
    # A long outline: sections with nested blocks.
    out = []
    for s in range(40):
        out.append("- ## Rozdział %d: %s" % (s + 1, sentence(rng, 2, 4)))
        for _ in range(rng.randint(5, 12)):
            out.append("  - %s" % sentence(rng, 4, 30))
            for _ in range(rng.randint(0, 3)):
                out.append("    - %s" % sentence(rng, 3, 15))
    with open(os.path.join(args.dir, "Długi konspekt.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    # A long document: headings, paragraphs, lists, a picture.
    out = ["---", "brulion: document", "---", "# Długi dokument", ""]
    for s in range(30):
        out.append("## %s" % sentence(rng, 2, 5))
        out.append("")
        for _ in range(rng.randint(2, 5)):
            out.append(" ".join(sentence(rng, 8, 22) + "." for _ in range(rng.randint(1, 4))))
            out.append("")
        if rng.random() < 0.6:
            for _ in range(rng.randint(2, 5)):
                out.append("- %s" % sentence(rng, 3, 12))
            out.append("")
        if s % 7 == 3:
            out.append("![[assets/plan.svg]]")
            out.append("")
    with open(os.path.join(args.dir, "Długi dokument.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("make-vault: %d journal days, %d pages in %s" % (written, len(PAGES) + 2, args.dir))


if __name__ == "__main__":
    main()
