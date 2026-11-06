---
tags: [dokumentacja]
brulion: document
---
# Kopie zapasowe

Brulion może sam robić kopie notatnika: zwykłe archiwa ZIP z Twoimi plikami. Historia wersji pomaga, gdy zepsujesz jedną notatkę; kopia pomaga, gdy zepsuje się więcej naraz. Wszystko ustawisz w **Ustawienia → Kopie zapasowe** (albo „kopie” w palecie poleceń).

## Co jest w kopii
Wszystkie notatki i załączniki bajt w bajt, ustawienia notatnika (`.brulion/config.json`) i puste foldery. Nie ma w niej kosza, `.git`, plików tymczasowych ani innych folderów, które Brulion pomija (na przykład `.obsidian` i `logseq`). Kopię możesz rozpakować dowolnym programem — nie potrzebujesz do tego Brulionu.

## Harmonogram
- **Kopie automatyczne** są domyślnie włączone; w przeglądarce domyślnie wyłączone (zobacz niżej).
- **Jak często:** codziennie albo co tydzień. Brulion sprawdza to przy otwarciu notatnika i co godzinę, gdy działa. Jeśli od ostatniej kopii nic się nie zmieniło, nowej nie robi.
- **Ile zachować:** najstarsze kopie znikają dopiero po udanym zapisie nowej. Brulion usuwa tylko kopie tego notatnika (każda kopia zapisuje, z jakiego notatnika powstała); inne pliki w folderze zostawia w spokoju.
- **Kopia niekompletna:** gdy jakiegoś pliku nie da się odczytać, kopia powstaje bez niego, a lista i ustawienia to mówią. Taka kopia nie wypiera pełnych, a następnego dnia Brulion próbuje znowu. Gdy podczas kopii zniknie cały dysk z notatnikiem, kopii nie będzie wcale.
- **Utwórz kopię teraz** robi kopię od razu, także gdy nic się nie zmieniło.

## Gdzie są kopie
Domyślnie w prywatnym folderze aplikacji na tym urządzeniu, osobno dla każdego notatnika. Synchronizacja ich nie roznosi, więc **kopia na tym samym dysku nie chroni przed awarią dysku**. Na komputerze i na Androidzie możesz wskazać inny folder, na przykład na pendrive'ie. Folder wewnątrz notatnika jest niedozwolony: synchronizacja rozniosłaby kopie, a każda kolejna zawierałaby poprzednie.

Jeśli dwa notatniki o tej samej nazwie mają kopie w jednym folderze, kopie drugiego są na liście oznaczone „nie z tego notatnika”: Brulion ich nie usuwa sam, a przywrócenie albo usunięcie takiej kopii wymaga osobnego potwierdzenia. Tak samo wyglądają kopie notatnika sprzed przeniesienia go do innego folderu.

Gdy folderu nie ma (odpięty dysk), Brulion pokazuje to w ustawieniach i w rejestrze zdarzeń, a starszych kopii nie rusza. Kopia powstanie przy następnym sprawdzeniu po podłączeniu dysku.

### Android
W prywatnym folderze aplikacji kopie zobaczysz tylko w Brulionie; przycisk **Udostępnij** przy kopii pozwala wysłać ją dalej. Usunięcie aplikacji usuwa ten folder razem z kopiami. Inny folder wymaga uprawnienia „Dostęp do wszystkich plików”. Dyski w chmurze nie są obsługiwane — Brulion powie to przy wyborze.

### Przeglądarka
**Kopie są w pamięci tej przeglądarki: wyczyszczenie jej danych usuwa je razem z notatkami.** Żeby mieć kopię gdzie indziej, użyj przycisku **Pobierz**. Każda kopia zajmuje miejsce przydzielone stronie i pamięć przy każdym otwarciu, dlatego kopie automatyczne są tu domyślnie wyłączone, a zachować można najwyżej trzy. Kopia, która się nie mieści, po prostu nie powstaje; starsze zostają.

## Przywracanie
Przy kopii na liście wybierz **Przywróć**. Brulion najpierw sprawdza całą kopię i pokazuje, ile plików zastąpi, ile wróci, a ile trafi do kosza, bo w kopii ich nie ma. Pliki o tej samej treści pomija. Po potwierdzeniu:

1. Zapisuje otwarte notatki i czeka, aż skończy się trwająca synchronizacja z Nextcloudem; do końca przywracania nowa się nie zacznie.
2. Robi **kopię bezpieczeństwa** obecnego stanu. Jeśli się nie uda albo nie obejmie pliku, który przywracanie by zmieniło, nie przywraca niczego.
3. Zapisuje pliki z kopii; zastąpione notatki zostają w historii wersji, a inne pliki tylko w kopii bezpieczeństwa. Pliku, którego nie da się odczytać, nie nadpisuje.
4. Przenosi nadmiarowe pliki do kosza — tylko jeśli wszystkie zapisy się udały. Z kopii niekompletnej nadmiarowe pliki zostają na miejscu.

Gdy przywracanie przerwie zamknięcie notatnika albo aplikacji, notatnik zostaje przywrócony tylko częściowo. Brulion powie o tym przy następnym otwarciu i wskaże kopię bezpieczeństwa; do czasu, aż potwierdzisz, że się tym zająłeś, nie robi kopii automatycznych. Dużo usuniętych plików nie znika od razu z serwera: Nextcloud jak zawsze poprosi o potwierdzenie w centrum synchronizacji. Syncthinga Brulion nie może wstrzymać. Przywrócić można kopię z listy w wybranym folderze; import dowolnego ZIP-a to osobna funkcja — zobacz [[Import i eksport|Import i eksport]].

Kopii z samym folderem albo samymi ustawieniami Brulion nie przywróci, bo tylko opróżniłaby notatnik. Na telefonie (i innych dyskach, które nie rozróżniają wielkości liter) odmówi też kopii z nazwami różniącymi się tylko wielkością liter, na przykład `Plan.md` i `plan.md` — taką kopię rozpakuj ręcznie.

Plik większy niż 1 GB jest w kopii, ale Brulion go nie przywróci: zostaje taki, jaki jest, a okno przywracania to powie. Taki plik wypakujesz z ZIP-a ręcznie.
