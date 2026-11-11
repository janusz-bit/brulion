---
tags: [documentation]
aliases: [Rozszerzenie, Luau, Konsola Luau, Skrypty]
brulion: document
---
# Rozszerzenia

Brulion potrafi uruchamiać kod, który mieszka w Twoim notatniku — tak jak Emacs wykonuje plik startowy, a TiddlyWiki trzyma własny kod w tiddlerach. **Rozszerzenie** to strona z tagiem `rozszerzenie` (albo `extension`): kod w jej blokach `luau` dodaje polecenia do palety, własne widoki i reakcje na zapis i zmiany. Językiem jest Luau, mały i szybki Lua z typami; kod czyta i zapisuje tylko otwarty notatnik, z limitem czasu i pamięci, i nigdy nie sięga do plików, sieci ani innych programów.

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

Otwórz paletę i uruchom **Powitanie**: na dole pojawi się komunikat. Zmień kod, a strona sama się zapisze — Brulion **od razu** wczyta go od nowa. Wszystko wokół bloków kodu (nagłówki, notatki, przykłady w innych językach) to dokumentacja dla Ciebie.

## Gdy coś nie działa
Błąd wskazuje **wiersz strony**: w rogu strony pojawia się „Błąd w wierszu 14: …” z przyciskiem do tego wiersza, to samo pokazują **Ustawienia → Rozszerzenia** i **konsola Luau**. Dopóki piszesz, działa wersja sprzed błędu — niedokończony kod nigdy nie zabiera działającego rozszerzenia.

## Kod z innych urządzeń
Strony przychodzą zewsząd: z synchronizacji, importu, wspólnego folderu. Dlatego kod strony działa na urządzeniu dopiero wtedy, gdy go tam **zatwierdzisz**: róg strony mówi „nowe albo zmienione gdzie indziej — przeczytaj i uruchom” z przyciskiem **Uruchom**, tak samo **Ustawienia → Rozszerzenia**. To, co napiszesz w Brulionie na tym urządzeniu, zostaje zatwierdzone; zmiana, która przyszła inną drogą, znowu czeka na Ciebie. Słowa wokół kodu nigdy nie wymagają ponownego zatwierdzenia.

Co rozszerzenie może robić poza czytaniem, jest zapisane na jego stronie:

```markdown
---
tags: [rozszerzenie]
uprawnienia: [zapis]
---
```

`zapis` (`write`) pozwala zmieniać notatki. Zatwierdzenie strony zatwierdza jej uprawnienia; prośba o więcej wymaga zatwierdzenia od nowa.

## Konsola Luau
**Konsola Luau** w palecie wykonuje po jednym wierszu Luau z API notatnika: `b.today()`, `#b.pages({})` (ile stron), `b.blocks({ tag = "projekt" })`. Wartość pojawia się poniżej; zmienne zostają na następny wiersz; ↑ i ↓ przywołują to, co już wpisałeś. Trafia tam też wszystko, co rozszerzenia wypisują przez `print()`, razem z ich błędami. **Uruchom blok Luau** (paleta, kursor w bloku `luau` dowolnej strony) wykonuje ten blok i pokazuje wynik w komunikacie.

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
Całe API `b`, limity i zasady zatwierdzania opisuje dokumentacja dla programistów: `docs/EXTENSIONS.md` w kodzie źródłowym. Format strony z rozszerzeniem: `docs/SYNTAX.md` §13.
