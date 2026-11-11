---
tags: [documentation]
brulion: document
---
# Siłownia

Siłownia to dziennik treningowy w Twoim dzienniku: serie zapisujesz jak zwykłe notatki, a Brulion pilnuje planu, dokłada ciężaru, gdy jesteś gotów, odmierza przerwę między seriami i rysuje postęp każdego ćwiczenia. Włącz ją w **Ustawieniach → Rozszerzenia → Siłownia**; wtedy **Siłownia** pojawi się na pasku bocznym i w palecie poleceń (Ctrl+K).

## Zapis treningu
Trening to punkt z tagiem `#trening` (albo `#workout`) w notatce dnia, a ćwiczenia to jego podpunkty:

```markdown
- Trening A #trening
  - [x] [[Przysiad]] 100 kg 5×5
  - [x] [[Wyciskanie leżąc]] 60 kg × 10; 80 kg × 5, 5, 4 @9
  - [x] [[Podciąganie]] MC × 10, 8, 6
  - [ ] [[Pompki na poręczach]] MC+10 kg 3×8
  - [[Deska]] 3×60 s
- [[Bieg]] 5 km 27:30 #trening
```

- `100 kg 5×5` — 5 serii po 5 powtórzeń ze 100 kg; `80 kg × 5, 5, 4` — trzy serie z ich powtórzeniami; grupy z innym ciężarem po `;`.
- `MC` — masa ciała, `MC+10 kg` z obciążeniem, `MC-20 kg` z odciążeniem; `3×60 s`, `10 min` — serie na czas; `5 km 27:30` — dystans i czas; `@8` — RPE.
- Na telefonie pisz, jak wygodnie: `100kg 3x5`, `× 5,5,4`, `4x8 60kg` też działają, a **Siłownia: popraw zapis serii** (paleta, kursor w wierszu) przepisze wiersz w pisowni Brulionu. Komentarze zostają: `(kolano boli)`, `— lżej`.
- Odhaczone zadanie albo zwykły punkt to historia; otwarte zadanie `[ ]` to plan na dziś.

## Plany
Plan to strona z tagiem `plan-treningowy` (albo `workout-plan`) z ćwiczeniami bez ciężarów:

```markdown
tags:: plan-treningowy
przerwa:: 3 min

- [[Przysiad]] 3×5
- [[Martwy ciąg]] 1×5
  progresja:: +5 kg
```

**Zacznij trening** (przycisk w widoku Siłownia albo paleta) pyta o plan i wpisuje go do dzisiejszej notatki jako zadania, każde z ciężarem z ostatniej sesji — o 2,5 kg większym (5 lb albo `progresja::` wiersza), gdy wszystkie zaplanowane serie wyszły tam w pełni. Pola wyboru ćwiczeń to serie, nie sprawy do załatwienia: widok Zadania ich nie pokazuje (zapytanie z `tag:trening` je pokaże).

## Widok Siłownia
- **Dziś**: dzisiejsze ćwiczenia jako karty — odhacz ćwiczenie, zmień ciężar i powtórzenia ostatniej serii przyciskami − / +, dodaj albo usuń serię. Każda zmiana przepisuje tylko ten jeden wiersz notatki. Dodanie serii uruchamia **timer przerwy** (`przerwa::` planu, inaczej 2 minuty) z powiadomieniem na koniec.
- **Postęp**: każde ćwiczenie z rekordem (szacowanym maksimum na jedno powtórzenie) i liczbą sesji; po kliknięciu wykres postępu ćwiczenia i wszystkie jego sesje.

**Siłownia: ćwiczenie z katalogu** zakłada stronę ćwiczenia spośród 51 popularnych (mięśnie, sprzęt), po polsku albo po angielsku, w języku Twoich notatek.

Wszystko zostaje zwykłym Markdownem: Obsidian i Logseq pokażą treningi jako zwykły konspekt z linkami i zadaniami. Siłownia to pierwsze rozszerzenie Brulionu, napisane w Luau: jej kod może stać się stroną Twojego notatnika i być zmieniany tam (zobacz [[Rozszerzenia]]).
