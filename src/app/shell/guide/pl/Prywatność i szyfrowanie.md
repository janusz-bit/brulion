---
tags: [documentation]
aliases: [Szyfrowanie, Zaszyfrowane notatki, Hasło, Blokada aplikacji, PIN, Meld Encrypt, Prywatność]
brulion: document
---
# Prywatność i szyfrowanie

Brulion ma dwa rodzaje ochrony, na dwa różne zmartwienia:

- **Zaszyfrowane notatki** — strona albo fragment strony czytelny tylko z hasłem. Plik na dysku, kopie na innych urządzeniach, serwer synchronizacji i kopie zapasowe mają tylko pomieszane znaki. Do haseł, PIN-ów, pamiętnika — wszystkiego, czego nikt nie powinien przeczytać, nawet jeśli zdobędzie folder notatnika.
- **Blokada aplikacji** — PIN (albo odcisk palca, twarz lub blokada ekranu telefonu) przed całą aplikacją na tym urządzeniu. Nie wpuści do Brulionu kogoś, kto trzyma Twój odblokowany telefon lub komputer; same notatki pozostają zwykłymi plikami.

## Zaszyfrowany tekst na stronie
Zaznacz tekst i wybierz **Zaszyfruj zaznaczenie…** z menu tekstu (prawy przycisk albo dłuższe przytrzymanie) albo wpisz `/encrypt`, aby dodać nowy zaszyfrowany tekst w miejscu kursora. Wybierz hasło, wpisz je dwa razy i — jeśli chcesz — **podpowiedź**. Podpowiedź nie jest szyfrowana, każdy może ją przeczytać.

Tekst zamieni się w zablokowany znaczek: 🔒 z podpowiedzią albo „Zaszyfrowane”. Kliknij go i wpisz hasło, aby przeczytać. W oknie, które się otworzy, możesz:
- zmienić tekst i **Zapisz** — zostanie zaszyfrowany ponownie tym samym hasłem;
- **Kopiuj** go (na Androidzie 13 i nowszym oraz w menedżerach schowka, które to respektują, kopia jest oznaczona jako poufna — mimo to w schowku nie jest zaszyfrowana);
- w **…**: **Zmień hasło…** albo **Odszyfruj na stałe…** (zwykły tekst wraca do strony).

W pliku znaczek zapisuje się jako `%%🔐γ 💡podpowiedź💡…🔐%%` — w widoku czytania Obsidiana jest ukryty jak komentarz. Włącz **Pokazuj kłódkę w widoku czytania Obsidiana** przy szyfrowaniu, aby zamiast tego zapisać `🔐γ … 🔐`, które Obsidian pokazuje jako 🔐 do kliknięcia.

## Zaszyfrowane strony
Zaszyfrować można też całą stronę: menu strony **…** → **Zaszyfruj stronę…** (albo paleta poleceń). Strona staje się plikiem `Nazwa.mdenc`, a wszystkie linki do niej podążają za nią; jej historia wersji na tym urządzeniu (wcześniejsze, niezaszyfrowane wersje) zostaje usunięta, chyba że odznaczysz tę opcję. Nowa zaszyfrowana strona: **Nowa zaszyfrowana strona…** w palecie poleceń albo w menu folderu na pasku bocznym.

Zaszyfrowana strona pokazuje kłódkę, dopóki nie wpiszesz hasła; potem otwiera się w edytorze jak każda inna, a wszystko, co piszesz, zapisuje się zaszyfrowane. Menu strony ma **Zmień hasło…**, **Odszyfruj na stałe…** i **Zablokuj stronę**. Zaszyfrowanej strony nie da się wyeksportować, wydrukować ani pokazać jako kodu QR — jeśli tego potrzebujesz, najpierw odszyfruj ją na stałe.

## Zapamiętywanie haseł
Wpisane hasło jest przez chwilę pamiętane, więc następny zaszyfrowany tekst albo strona z tym samym hasłem otworzy się bez pytania. Po **5 minutach** bez używania zaszyfrowanych notatek (Ustawienia → Prywatność: od 1 minuty do 1 godziny) wszystkie hasła zostają zapomniane, a otwarte zaszyfrowane strony znów się blokują — także gdy zablokuje się aplikacja, gdy zamkniesz notatnik i po poleceniu **Zablokuj zaszyfrowane notatki**. Hasła nigdy nigdzie nie są zapisywane.

> [!warning] Zapomnianego hasła nie da się odzyskać
> Nikt — ani Brulion, ani jego autor — nie otworzy tekstu bez hasła. Przechowuj je w bezpiecznym miejscu.

## Zgodność z Obsidianem
Brulion czyta i zapisuje format wtyczki **Meld Encrypt** do Obsidiana: te same notatki otwierają się w obu aplikacjach tym samym hasłem. Brulion czyta każdy format, jaki Meld Encrypt kiedykolwiek zapisał (także starsze znaczniki `α` i `β` oraz pliki `.mdenc` w wersjach 1.0 i 2.0), a zapisuje obecny (`γ`, plik w wersji 3.0: AES-256-GCM z kluczem z PBKDF2-SHA512, 220 000 rund). Starszy tekst zostaje zaktualizowany, gdy go zapiszesz.

## Co pozostaje czytelne
- Nazwa strony i folder, podpowiedź oraz to, że coś jest zaszyfrowane. Wyszukiwanie nie znajduje słów w zaszyfrowanym tekście, linki i tagi w nim nie pojawiają się w odnośnikach ani tagach, a przypomnienia i zadania w nim nigdy nie dzwonią i nie trafiają do Zadań.
- Kopie zrobione **zanim** coś zaszyfrowałeś — kopie zapasowe, wersje na innych urządzeniach, historia serwera synchronizacji — zostają, jakie były.
- Dopóki strona jest odblokowana, jej tekst jest w pamięci aplikacji (tak go czytasz).

## Synchronizacja
Zaszyfrowany tekst to zwykły tekst w pliku, więc synchronizacja scala go jak każdy wiersz: zmiany w różnych miejscach strony się scalają; gdy dwa urządzenia zmieniły ten sam zaszyfrowany tekst, obie wersje zostają zachowane jako konflikt, tak jak przy każdym innym wierszu. Cała zaszyfrowana strona zmieniona na dwóch urządzeniach naraz zostaje zachowana w obu wersjach (kopia konfliktu), nigdy wymieszana. Zaszyfrowana strona zmieniona na innym urządzeniu **innym** hasłem blokuje się tutaj i nigdy nie zostanie nadpisana — odblokuj ją nowym hasłem.

Cały notatnik można też zostawić nieczytelny na serwerze Nextcloud: [[Synchronizacja#Szyfrowanie end-to-end|Szyfrowanie end-to-end]]. Każda nazwa jest szyfrowana AES-256-SIV, a każdy plik AES-256-GCM w kawałkach po 64 KiB własnym kluczem; klucz notatnika jest losowy i leży na serwerze zapieczętowany Twoim hasłem (PBKDF2-HMAC-SHA-512, 600 000 rund) oraz kluczem odzyskiwania, a na każdym urządzeniu w pliku, który może czytać tylko Brulion.

## Blokada aplikacji
Ustawienia → **Prywatność** → **Ustaw PIN…** (od 4 do 16 cyfr). Na telefonie możesz zamiast tego użyć blokady telefonu: **Użyj blokady telefonu** — odcisku palca, twarzy albo PIN-u, wzoru lub hasła telefonu.

Sam wybierasz, kiedy Brulion się blokuje: przy uruchomieniu, po wyjściu z niego (od razu, po 1, 5 albo 15 minutach lub po godzinie), a na komputerze także po chwili bez klawisza, kliknięcia czy dotyku. **Zablokuj teraz** (Ustawienia albo **Zablokuj Brulion** w palecie poleceń) blokuje od razu. Gdy aplikacja jest zablokowana:
- nic z notatek nie jest widoczne, także w osobnych oknach; zaszyfrowane notatki też się blokują;
- przypomnienia pokazują tylko „Przypomnienie” i godzinę (**Ukrywaj treść notatki w powiadomieniach**);
- na Androidzie widżety na ekranie głównym pokazują „Brulion jest zablokowany” (szybkie dodawanie nadal działa), a ekran Brulionu nie pojawia się w podglądzie ostatnich aplikacji; **Ukrywaj ekran Brulionu** → **Zawsze** blokuje też zrzuty i nagrywanie ekranu.

Po pięciu błędnych PIN-ach Brulion każe poczekać — 30 sekund, potem dłużej, aż do 15 minut. Zapomniany PIN da się usunąć tylko, czyszcząc ustawienia Brulionu na tym urządzeniu; notatki pozostaną nietknięte. W przeglądarce dostępny jest tylko PIN.

Zobacz też: [[Synchronizacja|Synchronizacja]], [[Kopie zapasowe|Kopie zapasowe]], [[Format plików|Format plików]].
