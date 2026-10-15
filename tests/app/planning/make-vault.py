#!/usr/bin/env python3
"""Generate a big, realistic Polish test notebook for the planning views (screenshots, manual
performance checks): pages in folders with [[links]] (clustered, plus some long-range ones),
#tags, tasks with due / scheduled / start dates, priorities, recurrence and reminders, daily
notes around "today", a few links to pages that do not exist yet and very long titles.

    tests/app/planning/make-vault.py /tmp/Duży\\ brulion --pages 2000 --today 2026-10-14

Deterministic (seeded): the same arguments give the same files.
"""
import argparse
import datetime as dt
import os
import random

FOLDERS = ["Projekty", "Praca", "Dom", "Ludzie", "Książki", "Przepisy", "Podróże", "Zdrowie", "Nauka", "Notatki"]
NOUNS = ["ogród", "łazienka", "kuchnia", "wyjazd", "spotkanie", "budżet", "remont", "szkolenie", "przegląd",
         "prezentacja", "umowa", "raport", "pomysł", "plan", "lista", "przepis", "książka", "kurs", "projekt",
         "żółw", "rower", "samochód", "mieszkanie", "działka", "wesele", "urodziny", "konferencja", "wniosek"]
ADJECTIVES = ["nowy", "letni", "zimowy", "ważny", "szybki", "długi", "wspólny", "domowy", "roczny",
              "kwartalny", "rodzinny", "zielony", "drewniany", "świąteczny", "pilny", "ostatni"]
PLACES = ["Gdańsk", "Kraków", "Zakopane", "Wrocław", "Łódź", "Poznań", "Toruń", "Sopot", "Białystok", "Lublin"]
PEOPLE = ["Ania", "Kasia", "Marek", "Paweł", "Zosia", "Jędrzej", "Łukasz", "Małgosia", "Grażyna", "Bożena"]
TAGS = ["praca", "dom", "zdrowie", "pomysł", "zakupy", "finanse", "rodzina", "projekt/remont", "projekt/ogród",
        "do-przeczytania", "nauka", "podróże", "pilne"]
TASKS = ["Zadzwonić do {p}", "Kupić farbę do {n}", "Odebrać paczkę", "Zapłacić rachunek za prąd",
         "Przygotować prezentację", "Napisać raport kwartalny", "Umówić wizytę u dentysty", "Oddać książkę",
         "Zarezerwować nocleg w {c}", "Wysłać umowę do {p}", "Podlać kwiaty", "Wymienić żarówki",
         "Przejrzeć budżet", "Zamówić części do roweru", "Odpisać {p} na maila", "Zrobić przegląd auta",
         "Kupić prezent dla {p}", "Posprzątać piwnicę", "Zapisać się na kurs", "Przeczytać rozdział 3"]
RULES = ["every week", "every month", "every week on Monday", "every 2 weeks", "every day", "every month on the 10th"]
PRIORITIES = ["", "", "", "", "⏫", "🔼", "🔽", "🔺"]


def title_for(rng, i):
    kind = rng.random()
    if kind < 0.25:
        return f"{rng.choice(ADJECTIVES).capitalize()} {rng.choice(NOUNS)} {i}"
    if kind < 0.45:
        return f"Spotkanie z {rng.choice(PEOPLE)} {i}"
    if kind < 0.6:
        return f"Wyjazd do {rng.choice(PLACES)} {i}"
    if kind < 0.7:
        return f"{rng.choice(PEOPLE)} — notatki {i}"
    if kind < 0.72:
        # A very long title (labels and lists must elide it).
        return f"Bardzo długi tytuł strony o planowaniu wspólnego {rng.choice(NOUNS)}u i wszystkich szczegółach {i}"
    return f"{rng.choice(NOUNS).capitalize()} {rng.choice(PLACES)} {i}"


def task_line(rng, today, people, places):
    text = rng.choice(TASKS).format(p=rng.choice(people), n=rng.choice(NOUNS), c=rng.choice(places))
    parts = []
    status = "x" if rng.random() < 0.18 else ("/" if rng.random() < 0.05 else " ")
    prio = rng.choice(PRIORITIES)
    if prio:
        parts.append(prio)
    r = rng.random()
    if r < 0.55:
        due = today + dt.timedelta(days=rng.randint(-12, 45))
        parts.append(f"📅 {due.isoformat()}")
    elif r < 0.7:
        parts.append(f"⏳ {(today + dt.timedelta(days=rng.randint(-5, 20))).isoformat()}")
    if rng.random() < 0.08:
        parts.append(f"🔁 {rng.choice(RULES)}")
    if rng.random() < 0.15:
        when = today + dt.timedelta(days=rng.randint(0, 25))
        parts.append(f"⏰{'!' if rng.random() < 0.15 else ''} {when.isoformat()} {rng.randint(7, 20):02d}:{rng.choice([0, 15, 30, 45]):02d}")
    if rng.random() < 0.3:
        parts.append(f"#{rng.choice(TAGS)}")
    if status == "x":
        parts.append(f"✅ {(today - dt.timedelta(days=rng.randint(0, 30))).isoformat()}")
    return f"- [{status}] {text} " + " ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--pages", type=int, default=2000)
    ap.add_argument("--today", default=dt.date.today().isoformat())
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    today = dt.date.fromisoformat(args.today)
    os.makedirs(os.path.join(args.root, "journals"), exist_ok=True)

    pages = []
    for i in range(args.pages):
        folder = FOLDERS[(i * 7 + rng.randint(0, 2)) % len(FOLDERS)]
        pages.append((folder, title_for(rng, i)))

    for i, (folder, title) in enumerate(pages):
        lines = [f"# {title}", ""]
        if rng.random() < 0.4:
            lines.insert(0, f"---\ntags: [{rng.choice(TAGS)}]\n---")
        for _ in range(rng.randint(1, 5)):
            # Mostly neighbours (clusters), sometimes far away, rarely a page that does not exist.
            r = rng.random()
            if r < 0.75:
                target = pages[(i + rng.randint(1, 15)) % len(pages)][1]
            elif r < 0.97:
                target = rng.choice(pages)[1]
            else:
                target = f"Nieistniejąca strona {rng.randint(1, 40)}"
            lines.append(f"- Zobacz [[{target}]] #{rng.choice(TAGS)}")
        for _ in range(rng.randint(0, 3)):
            lines.append(task_line(rng, today, PEOPLE, PLACES))
        path = os.path.join(args.root, folder, title + ".md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    for d in range(-120, 1):
        day = today + dt.timedelta(days=d)
        if rng.random() < 0.25 and d < 0:
            continue
        lines = []
        for _ in range(rng.randint(1, 6)):
            r = rng.random()
            if r < 0.4:
                lines.append(f"- Rozmowa o [[{rng.choice(pages)[1]}]] z {rng.choice(PEOPLE)}")
            elif r < 0.75:
                lines.append(task_line(rng, today, PEOPLE, PLACES))
            else:
                when = today + dt.timedelta(days=rng.randint(0, 20))
                lines.append(f"- Przypomnienie: {rng.choice(TASKS).format(p=rng.choice(PEOPLE), n=rng.choice(NOUNS), c=rng.choice(PLACES))} ⏰ {when.isoformat()} {rng.randint(7, 21):02d}:00")
        with open(os.path.join(args.root, "journals", day.isoformat() + ".md"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    print(args.root)


if __name__ == "__main__":
    main()
