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
- **Ile zachować:** najstarsze kopie znikają dopiero po udanym zapisie nowej. Brulion usuwa tylko kopie, które sam zrobił dla tego notatnika; inne pliki w folderze zostawia w spokoju.
- **Utwórz kopię teraz** robi kopię od razu, także gdy nic się nie zmieniło.

## Gdzie są kopie
Domyślnie w prywatnym folderze aplikacji na tym urządzeniu, osobno dla każdego notatnika. Synchronizacja ich nie roznosi, więc **kopia na tym samym dysku nie chroni przed awarią dysku**. Na komputerze i na Androidzie możesz wskazać inny folder, na przykład na pendrive'ie. Folder wewnątrz notatnika jest niedozwolony: synchronizacja rozniosłaby kopie, a każda kolejna zawierałaby poprzednie.

Gdy folderu nie ma (odpięty dysk), Brulion pokazuje to w ustawieniach i w rejestrze zdarzeń, a starszych kopii nie rusza. Kopia powstanie przy następnym sprawdzeniu po podłączeniu dysku.

### Android
W prywatnym folderze aplikacji kopie zobaczysz tylko w Brulionie; przycisk **Udostępnij** przy kopii pozwala wysłać ją dalej. Usunięcie aplikacji usuwa ten folder razem z kopiami. Inny folder wymaga uprawnienia „Dostęp do wszystkich plików”. Dyski w chmurze nie są obsługiwane — Brulion powie to przy wyborze.

### Przeglądarka
**Kopie są w pamięci tej przeglądarki: wyczyszczenie jej danych usuwa je razem z notatkami.** Żeby mieć kopię gdzie indziej, użyj przycisku **Pobierz**. Każda kopia zajmuje miejsce przydzielone stronie i pamięć przy każdym otwarciu, dlatego kopie automatyczne są tu domyślnie wyłączone, a zachować można najwyżej trzy. Kopia, która się nie mieści, po prostu nie powstaje; starsze zostają.

## Przywracanie
Przy kopii na liście wybierz **Przywróć**. Brulion najpierw sprawdza całą kopię i pokazuje, ile plików zastąpi, ile wróci, a ile trafi do kosza, bo w kopii ich nie ma. Pliki o tej samej treści pomija. Po potwierdzeniu:

1. Robi **kopię bezpieczeństwa** obecnego stanu. Jeśli się nie uda, nie przywraca niczego.
2. Zapisuje pliki z kopii; zastąpione notatki zostają w historii wersji.
3. Przenosi nadmiarowe pliki do kosza — tylko jeśli wszystkie zapisy się udały.

Dużo usuniętych plików nie znika od razu z serwera: Nextcloud jak zawsze poprosi o potwierdzenie w centrum synchronizacji. Przywrócić można kopię z listy w wybranym folderze; import dowolnego ZIP-a to osobna funkcja — zobacz [[Import i eksport|Import i eksport]].

Plik większy niż 1 GB jest w kopii, ale Brulion go nie przywróci: zostaje taki, jaki jest, a okno przywracania to powie. Taki plik wypakujesz z ZIP-a ręcznie.
