---
tags: [dokumentacja]
aliases: [Rozszerzenie, Luau, Konsola Luau, Skrypty]
brulion: document
---
# Rozszerzenia

Brulion potrafi uruchamiać kod, który mieszka w Twoim notatniku — tak jak Emacs wykonuje plik startowy, a TiddlyWiki trzyma własny kod w tiddlerach. **Rozszerzenie** to strona z tagiem `rozszerzenie` (albo `extension`): kod w jej blokach `luau` dodaje polecenia do palety, własne widoki i reakcje na zapis i zmiany. Językiem jest Luau, mały i szybki Lua z typami; kod czyta i zapisuje tylko otwarty notatnik, z limitem czasu i pamięci, i nigdy nie sięga do plików, sieci ani innych programów.

Ta strona pokazuje, jak zacząć. [[API rozszerzeń]] wymienia wszystko, co kod może wywołać, a [[Przepisy na rozszerzenia]] to gotowe kawałki do skopiowania.

## Pierwsze rozszerzenie
**Ustawienia → Rozszerzenia → Nowe rozszerzenie** (albo **Nowe rozszerzenie** w palecie, Ctrl+K) zapisuje w folderze `Rozszerzenia` stronę z przykładem i ją otwiera:

````markdown
---
tags: [rozszerzenie]
opis: Moje pierwsze rozszerzenie.
---
```luau
b.command("powitanie", function(ctx)
    return "Dzień dobry! Dziś jest " .. b.today()
end, { label = "Powitanie" })
```
````

Otwórz paletę i uruchom **Powitanie**: tekst, który zwraca funkcja, pojawi się na dole jako komunikat. Zmień kod, a strona sama się zapisze — Brulion **od razu** wczyta go od nowa. Wszystko wokół bloków kodu (nagłówki, notatki, przykłady w innych językach) to dokumentacja dla Ciebie.

## Jak to działa
Kod strony wykonuje się raz, przy wczytaniu, od góry do dołu, i mówi, co rozszerzenie ma: `b.command` — polecenie, `b.view` — widok, `b.on` — reakcję. Potem Brulion wywołuje te funkcje: polecenie, gdy je wybierzesz, widok, gdy jest na ekranie. Wszystko, co kod może zrobić z notatnikiem, jest w jednej tabeli `b`: `b.today()`, `b.pages()`, `b.read("Dom")`, `b.query("type:tasks status:open")` i reszta z [[API rozszerzeń]].

Zmienne żyją tylko do następnego zapisu strony albo do zamknięcia Brulionu. To, co ma zostać, trzymaj w notatkach.

## Polecenie o stronie na ekranie
Polecenie dostaje `ctx`: `ctx.page` to nazwa strony na ekranie (pusty tekst, gdy żadnej nie ma). To, co funkcja zwróci, pojawia się jako komunikat.

```luau
b.command("policz-slowa", function(ctx)
    if ctx.page == "" then
        return "Najpierw otwórz stronę."
    end
    local slowa = 0
    for _ in (b.read(ctx.page) or ""):gmatch("%S+") do
        slowa += 1
    end
    return `{ctx.page}: {slowa} słów`
end, { label = "Policz słowa tej strony" })
```

Z `block = true` po etykiecie polecenie działa na bloku pod kursorem: `ctx.block` ma jego tekst, stan zadania i `ref`, dzięki któremu kod może go zmienić, a paleta pokazuje polecenie tylko wtedy, gdy jesteś w notatce.

## Pytania i zmiany
`b.ask("…")` prosi Cię o wiersz tekstu, `b.choose("…", { "A", "B" })` pokazuje listę do wyboru, `b.form` pyta o liczby z − i +. Żeby zmieniać notatki, strona potrzebuje uprawnienia `zapis`:

````markdown
---
tags: [rozszerzenie]
uprawnienia: [zapis]
---
```luau
b.command("notatka", function(ctx)
    local tekst = b.ask("Co zanotować?")
    if tekst == "" then
        return nil
    end
    b.appendToJournal(b.today(), "- " .. os.date("%H:%M") .. " " .. tekst)
    return "Zanotowano w dzisiejszym dzienniku"
end, { label = "Notatka z godziną" })
```
````

`b.appendToJournal` dopisuje do notatki dnia, `b.write` tworzy albo zastępuje stronę, `b.replaceBlock` zmienia jeden wiersz — i odmawia, gdy wiersz zmienił się, odkąd kod go przeczytał, więc synchronizacja, która przyjdzie w trakcie, nigdy nie zostanie nadpisana.

## Gdy coś nie działa
Błąd wskazuje **wiersz strony**: w rogu strony pojawia się „Błąd w wierszu 14: …” z przyciskiem do tego wiersza, to samo pokazują **Ustawienia → Rozszerzenia** i **konsola Luau**. Dopóki piszesz, działa wersja sprzed błędu — niedokończony kod nigdy nie zabiera działającego rozszerzenia. `print(…)` pisze do konsoli Luau: to najszybszy sposób, żeby zobaczyć, co kod ma w ręku. Komunikaty, które możesz spotkać, objaśnia [[API rozszerzeń#Błędy]].

## Kod z innych urządzeń
Strony przychodzą zewsząd: z synchronizacji, importu, wspólnego folderu. Dlatego kod strony działa na urządzeniu dopiero wtedy, gdy go tam **zatwierdzisz**: róg strony mówi „nowe albo zmienione gdzie indziej — przeczytaj i uruchom” z przyciskiem **Uruchom**, tak samo **Ustawienia → Rozszerzenia**. To, co napiszesz w Brulionie na tym urządzeniu, zostaje zatwierdzone; zmiana, która przyszła inną drogą, znowu czeka na Ciebie. Słowa wokół kodu nigdy nie wymagają ponownego zatwierdzenia.

Co rozszerzenie może robić poza czytaniem, jest zapisane na jego stronie:

```markdown
---
tags: [rozszerzenie]
uprawnienia: [zapis]
---
```

`zapis` (`write`) pozwala zmieniać notatki, `ai` — pytać model AI (zobacz [[AI]]). Zatwierdzenie strony zatwierdza jej uprawnienia; prośba o więcej wymaga zatwierdzenia od nowa.

## Konsola Luau
**Konsola Luau** w palecie wykonuje po jednym wierszu Luau z API notatnika: `b.today()`, `#b.pages({})` (ile stron), `b.blocks({ tag = "projekt" })`. Wartość pojawia się poniżej; zmienne zostają na następny wiersz; ↑ i ↓ przywołują to, co już wpisałeś. Trafia tam też wszystko, co rozszerzenia wypisują przez `print()`, razem z ich błędami. **Uruchom blok Luau** (paleta, kursor w bloku `luau` dowolnej strony) wykonuje ten blok i pokazuje wynik w komunikacie.

Działa to także w tej dokumentacji: ustaw kursor w kodzie „Policz słowa tej strony” powyżej i wybierz **Uruchom blok Luau** — polecenie będzie w palecie do ponownego wczytania rozszerzeń. Tak samo wypróbujesz przykłady ze stron [[API rozszerzeń]] i [[Przepisy na rozszerzenia]].

## Widoki i reakcje
Widok to funkcja, która zwraca, co pokazać, złożona z elementów Brulionu (`b.ui.heading`, `text`, `link`, `button`, `check`, `stepper`, `table`, `chart`); z etykietą (`label`) jest w palecie:

```luau
b.view("projekty", function(params)
    local linki = {}
    for _, nazwa in b.pages({ tag = "projekt" }) do
        table.insert(linki, b.ui.link(nazwa))
    end
    return b.ui.column({ b.ui.heading("Projekty"), b.ui.column(linki) })
end, nil, { label = "Projekty" })
```

Reakcja działa, gdy coś się dzieje: `b.on("save", function(e) … end)` po zapisaniu strony na tym urządzeniu (`e.page`), `b.on("change", function(e) … end)` po zmianach w notatkach, skądkolwiek przyszły (`e.pages`). Reakcja, która ciągle się wywraca, zostaje wyłączona do ponownego wczytania rozszerzenia.

## Kod Siłowni
[[Siłownia]] też jest rozszerzeniem. **Ustawienia → Rozszerzenia → Kod Siłowni → Edytuj kod** zapisuje jej źródło na stronie Twojego notatnika, sekcja pod nagłówkiem, i odtąd Siłownia działa **z tej strony**: zmień, zapisz i zmiana działa. **Przywróć** przenosi stronę do kosza i znowu działa wbudowana Siłownia.

## Więcej
- [[API rozszerzeń]] — każda funkcja `b`, jak wygląda blok, limity i komunikaty błędów.
- [[Przepisy na rozszerzenia]] — polecenia, widoki i reakcje do skopiowania.
- [[AI]] — `b.ai`: pytanie modelu z własnego kodu.
- Dla tych, którzy budują Brulion: `docs/EXTENSIONS.md` i `docs/SYNTAX.md` §13 w kodzie źródłowym.
