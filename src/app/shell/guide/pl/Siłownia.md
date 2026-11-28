---
tags: [dokumentacja]
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
- **Dziś**: dzisiejsze ćwiczenia jako karty — odhacz ćwiczenie, zmień ciężar i powtórzenia ostatniej serii przyciskami − / + albo je wpisz (stuknij liczbę, wpisz `32,5`, potem Enter albo stuknij obok), dodaj albo usuń serię. Każda zmiana przepisuje tylko ten jeden wiersz notatki. Dodanie serii uruchamia **timer przerwy** (`przerwa::` planu, inaczej 2 minuty) z powiadomieniem na koniec — na Androidzie przychodzi ono także przy zamkniętym Brulionie.
- **Postęp**: każde ćwiczenie z rekordem (szacowanym maksimum na jedno powtórzenie) i liczbą sesji; po kliknięciu wykres postępu ćwiczenia i wszystkie jego sesje.
- **Objętość**: serie × powtórzenia × ciężar na każdy mięsień w kolejnych tygodniach — ten tydzień wobec średniej poprzednich, wykres tygodni i mapa ciepła: wiersz na mięsień, kwadrat na tydzień, tym mocniejszy, im więcej pracy (stuknij kwadrat, żeby zobaczyć liczby). Mięśnie główne ćwiczenia dostają całą jego objętość, pomocnicze połowę; serie z masą ciała liczą się z masą z dziennika (niżej).
- **Waga**: dziennik masy ciała. Wpisz pomiar — `82,5`, `82,5 kg 18 % rano` — i Enter: do dzisiejszej notatki trafia `- 82,5 kg 18 % rano #waga`. Wykres pokazuje pomiary i trend (średnią wygładzoną o 10 % na dzień), kafelki ostatni pomiar, trend i jego zmianę w 7 i 30 dni.

Jednostkę (kg albo lb) wybierasz w **Ustawieniach → Rozszerzenia → Siłownia → Ustawienia**.

## Masa ciała
Pomiar to zwykły punkt dziennika z tagiem `#waga` (albo `#weight`):

```markdown
- 82,5 kg 18 % rano #waga
```

Masa z `kg` albo `lb` (sama liczba: jednostka z ustawień), opcjonalnie tkanka tłuszczowa w procentach, a pozostałe słowa to notatka. Kilka pomiarów jednego dnia: liczy się ostatni. Szybko zapiszesz go też z palety (**Siłownia: zapisz masę ciała**), a `{{ext:silownia/waga}}` w dowolnej notatce pokaże ostatni pomiar i trend.

## Katalog ćwiczeń
**Katalog ćwiczeń** (przycisk w widoku Dziś albo paleta) to ponad 870 ćwiczeń: 51 Brulionu i cała baza free-exercise-db (domena publiczna), popularne z polskimi nazwami. Szukaj po nazwie w dowolnym języku, bez polskich znaków też (`wioslowanie`), i zawężaj mięśniem, sprzętem albo rodzajem. Ćwiczenie pokazuje swoje mięśnie, dwa zdjęcia ruchu (początek i koniec) i opis wykonania (po polsku); możesz dopisać je do dzisiejszego treningu albo założyć jego stronę:

```markdown
katalog:: back-squat
miesnie:: czworogłowe uda
miesnie-pomocnicze:: łydki, pośladki, dwugłowe uda, dolna część pleców
sprzet:: sztanga
```

Zdjęć nie ma w aplikacji: pochodzą z internetu (free-exercise-db na GitHubie) i po pokazaniu zostają w pamięci podręcznej urządzenia. Jak obrazy z internetu w notatkach pobierają się dopiero po stuknięciu **Pokaż obrazy** — albo same, gdy w Ustawieniach → Edytor jest włączone **Pokazuj obrazy z internetu bez pytania**.

Mięśnie ze strony ćwiczenia (`miesnie::`, `miesnie-pomocnicze::`) liczą się w objętości przed katalogiem — tak przypiszesz mięśnie własnemu ćwiczeniu. **Siłownia: ćwiczenie z katalogu** w palecie pyta o nazwę i od razu zakłada stronę.

Wszystko zostaje zwykłym Markdownem: Obsidian i Logseq pokażą treningi jako zwykły konspekt z linkami i zadaniami. Siłownia to pierwsze rozszerzenie Brulionu, napisane w Luau: jej kod może stać się stroną Twojego notatnika i być zmieniany tam (zobacz [[Rozszerzenia]]).
