<!--
  Brulion's privacy policy. Published at https://janusz-bit.github.io/brulion/privacy.html:
  scripts/fdroid-repo.sh renders this file (scripts/markdown-page.py, which knows a small part of
  Markdown: headings, paragraphs, lists, tables, **bold**, `code` and links) every time the site is
  built, and a change to it on main rebuilds the site (.github/workflows/fdroid.yml). The app links
  it (About, Settings → Privacy); Google Play has it as the privacy policy (docs/GOOGLE_PLAY.md).
  Keep it true to the code: a new connection, a new permission or anything that may send data
  somewhere is written here, in both languages, with the date below.
-->

# Brulion — privacy policy

Effective date: 23 November 2026. [Polska wersja poniżej.](#polityka-prywatnosci)

Brulion is a notes app that works offline: your notes are plain Markdown files on your device. It
is free software (AGPL-3.0-or-later) made by an individual developer, janusz-bit
([source code](https://github.com/janusz-bit/brulion)). This policy covers Brulion on every
platform — the Android app from Google Play (`io.github.januszbit.brulion.play`), the Android app
from GitHub and F-Droid (`io.github.januszbit.brulion`), the desktop versions and the browser
version.

## In short

* **The developer receives nothing.** Brulion has no account, no analytics, no advertising, no
  crash reporting and no tracking. The developer runs no server that the app talks to, and gets
  none of your notes, settings or usage data.
* **Your notes stay on your device.** They leave it only when you turn on a feature that sends
  them somewhere — to a server or a service that you choose and set up yourself.
* **The one exception is a report you choose to send.** If you report an AI answer, the report
  goes to the developer only when you send it yourself, by e-mail or as a public GitHub issue
  (see "Reporting an AI answer").

## Where your notes are

* **On Android**, the notebook is in the app's private storage, or in a folder you choose. In the
  app from Google Play you pick the folder in Android's own folder dialog (for example a folder
  that Syncthing syncs); Brulion keeps its own copy of the notebook in its private storage in
  sync with that folder, on the device — nothing leaves the device for this. The app from GitHub
  and F-Droid can also use a folder on the shared storage, with the "All files access"
  permission.
* **On a computer**, the notebook is a folder you choose. If you add Claude Code or Codex as an AI
  agent there, Brulion reads their own session logs on that computer to show how many tokens they
  used and what that would cost on the provider's API; nothing of these logs leaves the computer,
  and the totals are kept in Brulion's cache folder.
* **In the browser version**, notes are kept in your browser's storage on that device. The page
  itself is served by GitHub Pages, which sees your IP address when it is loaded (GitHub's privacy
  statement applies to that).
* **Android's own backup**: Brulion allows Android to back up its data to your Google account and
  to move it to a new phone, under your system settings. On Android 12 and newer a cloud backup
  includes your notes only when it is end-to-end encrypted; passwords, tokens and downloaded
  models are always left out.

## When Brulion connects to the internet

Only for features you turn on or start, and only to the services you choose. Brulion never sends
your notes anywhere on its own.

* **Sync with Nextcloud or another WebDAV server** (off until you set it up): the notebook's
  files go to and come from the server address you enter, with the user name and app password
  you enter, in the background as well. The file names of conflict copies carry your device's
  name.
* **Calendars** (off until you add one): calendar subscriptions you add are downloaded from their
  addresses; CalDAV accounts you add sync your tasks and events both ways with your server.
* **A notebook on GitHub** (off until you set it up): Brulion downloads new versions of the
  repository you name from GitHub (`api.github.com`, `raw.githubusercontent.com`) and, when you
  ask, commits your changes or opens a pull request, with the access token you give it.
* **Publishing** (when you publish): the pages you choose go to the place you choose — GitHub
  Pages, your WebDAV server or a public Nextcloud link.
* **The AI assistant** (off until you turn it on): when you ask it something, your message, the
  parts of your notebook it reads to answer, and pictures you attach are sent to the AI provider
  you chose with your own API key — Anthropic (`api.anthropic.com`), OpenAI (`api.openai.com`) or
  any compatible server whose address you enter. That provider's own terms and privacy policy
  apply to what it receives. Extensions in your notebook may use the assistant only after you
  approve it.
* **A model on your device**: Brulion can run an AI model, the model of search by meaning and the
  speech recognition model (Whisper) on the device itself; what they read never leaves it. Their
  files are downloaded from Hugging Face (`huggingface.co`) only when you press the download
  button; Hugging Face sees your IP address, nothing about your notes.
* **An AI agent on a computer of yours** (when you connect one): the AI chat talks to
  `brulion-cli` running on your own computer, at the address you give; your messages and the
  notes the agent works with go to that computer, and pairing tells it your device's name. While
  it answers, Android shows a notification (a foreground service).
* **Pictures from the internet in notes**: a picture linked in a note is downloaded only when you
  tap "Show", or automatically if you turn that on. Its server sees your IP address; Brulion
  sends no cookies and no referrer.
* **Imports** (when you start one): importing from Microsoft To Do or OneNote, Todoist or
  Airtable signs in with your account or token, which is kept only in memory during the import.
* **Checking for a new version**: not in the app from Google Play. The app from GitHub or F-Droid
  and the Windows version ask GitHub (`api.github.com`) at most once a day whether there is a newer
  release; GitHub sees your IP address, nothing else is sent. It can be turned off in the
  settings.
* **On a computer** you can also let other programs reach your notebook — the `brulion-cli`
  command line, an MCP server for AI agents, the Brulion Web Clipper in your browser, AI agents
  shared with your phone. They run only when you set them up, and what those programs do with
  your notes is governed by their own terms.

Addresses Brulion knows are reached over HTTPS. If you enter a plain `http://` address for a
server, Brulion warns you, and what goes to that server is not encrypted on the way.

## Reporting an AI answer

You can report an answer of the AI assistant that is wrong, offensive or harmful. The report is
prefilled in your e-mail app or on GitHub's page for a new issue, where you see everything it
contains (such as the answer and your comment) and can change it. It reaches the developer only
if you send it; an issue on GitHub is public. You can also flag or hide the answer in the app,
which stays on your device.

## Passwords, tokens and the event log

* Passwords, API keys and tokens you enter are kept in the app's private storage on the device,
  outside your notes. They are never synced and are left out of Android's backup and of the
  transfer to a new phone.
* Brulion keeps an event log on the device to help you understand problems (for example a sync
  error). It may contain server addresses, user names and note names, never passwords, tokens or
  the text of notes. It leaves the device only when you share or save it yourself.
* Encrypted notes and the app lock (a PIN or the phone's own lock) are available if you want
  them; the PIN is stored only as a hash on the device.

## Android permissions

| Permission | What Brulion uses it for |
|---|---|
| Internet, network state | The features above that connect to a server; waiting for a network before syncing |
| Notifications | Reminders, and the notification of an AI agent at work |
| Alarms & reminders (exact alarms) | Reminders and alarms at the exact minute |
| Full-screen notifications | The alarm screen over the lock screen |
| Run at startup | Setting reminders again after the phone restarts |
| Vibration | Reminders and alarms |
| Keep the device awake, foreground service (data sync) | An AI agent on your computer answering while the screen is off |
| Biometrics | The app lock with your fingerprint, face or screen lock |
| Microphone | Voice notes, recorded only while you record; they are saved in your notebook and transcribed on the device |
| Change audio settings | Playing and recording sound through the right speaker or headset |

Brulion has no camera permission: a photo for the AI chat is taken by your camera app. Dictation
in the AI chat uses your phone's speech recognition app, which handles the sound under its own
terms; Brulion receives only the text. Notes and files shared to Brulion from other apps
("Share → Brulion") and automation apps' commands stay on the device. The app from GitHub and
F-Droid also asks for "All files access" (a notebook folder on the shared storage) and has the
"alarms" permission for alarm clocks; the app from Google Play has neither.

## Children

Brulion is not directed at children under 13.

## Deleting your data

The developer holds no data about you, so there is nothing to delete on the developer's side.
Uninstalling Brulion deletes its private storage: the notebooks kept there, passwords and tokens,
downloaded models and the event log. A folder you chose yourself stays where it is. Data you sent
to your own servers or to services you chose (your Nextcloud, GitHub, an AI provider) is deleted
there, under their rules.

## Changes and contact

A change to this policy is published at this address with a new effective date; every earlier
version is in the history of
[docs/PRIVACY.md](https://github.com/janusz-bit/brulion/commits/main/docs/PRIVACY.md). Questions:
an [issue on GitHub](https://github.com/janusz-bit/brulion/issues), or the developer's e-mail
address shown on Brulion's page in Google Play.

---

<!-- section lang="pl" -->

# Polityka prywatności

Obowiązuje od 23 listopada 2026 r.

Brulion to aplikacja do notatek, która działa offline: notatki są zwykłymi plikami Markdown na
Twoim urządzeniu. To wolne oprogramowanie (AGPL-3.0-or-later) tworzone przez jedną osobę, janusz-bit
([kod źródłowy](https://github.com/janusz-bit/brulion)). Ta polityka dotyczy Brulionu na każdej
platformie — aplikacji na Androida z Google Play (`io.github.januszbit.brulion.play`), aplikacji na
Androida z GitHuba i F-Droid (`io.github.januszbit.brulion`), wersji na komputer i wersji w
przeglądarce.

## W skrócie

* **Twórca nie dostaje niczego.** Brulion nie ma kont, analityki, reklam, zgłaszania awarii ani
  śledzenia. Twórca nie prowadzi żadnego serwera, z którym łączy się aplikacja, i nie dostaje
  Twoich notatek, ustawień ani danych o korzystaniu z aplikacji.
* **Notatki zostają na Twoim urządzeniu.** Opuszczają je tylko wtedy, gdy włączysz funkcję, która
  je gdzieś wysyła — na serwer albo do usługi, które wybierasz i konfigurujesz.
* **Jedynym wyjątkiem jest zgłoszenie, które zdecydujesz się wysłać.** Gdy zgłaszasz odpowiedź AI,
  zgłoszenie trafia do twórcy tylko wtedy, gdy je wyślesz — e-mailem albo jako publiczne
  zgłoszenie (issue) na GitHubie (zob. „Zgłaszanie odpowiedzi AI”).

## Gdzie są notatki

* **Na Androidzie** notatnik jest w prywatnej pamięci aplikacji albo w wybranym folderze. W
  aplikacji z Google Play folder wybierasz w systemowym oknie wyboru folderu (np. folder
  synchronizowany przez Syncthing); Brulion trzyma własną kopię notatnika w swojej pamięci
  prywatnej i synchronizuje ją z tym folderem na urządzeniu — nic przy tym nie opuszcza
  urządzenia. Aplikacja z GitHuba i F-Droid może też używać folderu w pamięci współdzielonej, z
  uprawnieniem „Dostęp do wszystkich plików”.
* **Na komputerze** notatnik jest folderem, który wybierasz. Jeśli dodasz tam Claude Code lub
  Codex jako agenta AI, Brulion czyta ich własne logi sesji na tym komputerze, by pokazać, ile
  tokenów zużyły i ile kosztowałoby to w API dostawcy; nic z tych logów nie opuszcza komputera, a
  sumy są trzymane w folderze pamięci podręcznej Brulionu.
* **W wersji w przeglądarce** notatki są w pamięci przeglądarki na tym urządzeniu. Samą stronę
  udostępnia GitHub Pages, który przy jej wczytaniu widzi Twój adres IP (dotyczy tego oświadczenie
  o prywatności GitHuba).
* **Kopia zapasowa Androida**: Brulion pozwala Androidowi kopiować swoje dane na Twoje konto
  Google i przenosić je na nowy telefon, zgodnie z ustawieniami systemu. Na Androidzie 12 i nowszym
  kopia w chmurze obejmuje notatki tylko wtedy, gdy jest szyfrowana end-to-end; hasła, tokeny i
  pobrane modele są zawsze pomijane.

## Kiedy Brulion łączy się z internetem

Tylko przy funkcjach, które włączasz albo uruchamiasz, i tylko z usługami, które wybierasz. Brulion
nigdy sam z siebie nie wysyła nigdzie Twoich notatek.

* **Synchronizacja z Nextcloudem albo innym serwerem WebDAV** (wyłączona, dopóki jej nie
  skonfigurujesz): pliki notatnika są wysyłane na podany adres serwera i z niego pobierane, z
  podaną nazwą użytkownika i hasłem aplikacji, także w tle. Nazwy kopii konfliktowych zawierają
  nazwę urządzenia.
* **Kalendarze** (wyłączone, dopóki żadnego nie dodasz): dodane subskrypcje kalendarzy są pobierane
  z ich adresów; dodane konta CalDAV synchronizują zadania i wydarzenia w obie strony z Twoim
  serwerem.
* **Notatnik na GitHubie** (wyłączony, dopóki go nie skonfigurujesz): Brulion pobiera nowe wersje
  wskazanego repozytorium z GitHuba (`api.github.com`, `raw.githubusercontent.com`), a na Twoje
  polecenie zatwierdza zmiany albo otwiera pull request, z podanym tokenem dostępu.
* **Publikowanie** (gdy publikujesz): wybrane strony trafiają w wybrane miejsce — na GitHub
  Pages, na Twój serwer WebDAV albo pod publiczny link Nextclouda.
* **Asystent AI** (wyłączony, dopóki go nie włączysz): gdy o coś pytasz, Twoja wiadomość, części
  notatnika, które asystent czyta, by odpowiedzieć, i dołączone obrazy trafiają do wybranego
  dostawcy AI z Twoim własnym kluczem API — Anthropic (`api.anthropic.com`), OpenAI
  (`api.openai.com`) albo zgodnego serwera, którego adres podasz. Do tego, co dostaje dostawca,
  stosują się jego własne warunki i polityka prywatności. Rozszerzenia w notatniku mogą korzystać
  z asystenta dopiero po Twojej zgodzie.
* **Model na urządzeniu**: Brulion może uruchamiać na samym urządzeniu model AI, model
  wyszukiwania po znaczeniu i model rozpoznawania mowy (Whisper); to, co czytają, nie opuszcza
  urządzenia. Ich pliki są pobierane z Hugging Face (`huggingface.co`) dopiero po naciśnięciu
  przycisku pobierania; Hugging Face widzi Twój adres IP, nic o notatkach.
* **Agent AI na Twoim komputerze** (gdy go podłączysz): czat AI rozmawia z `brulion-cli`
  uruchomionym na Twoim komputerze, pod podanym adresem; Twoje wiadomości i notatki, nad którymi
  pracuje agent, trafiają na ten komputer, a przy parowaniu poznaje on nazwę urządzenia. Gdy agent
  odpowiada, Android pokazuje powiadomienie (usługa na pierwszym planie).
* **Obrazy z internetu w notatkach**: obraz podlinkowany w notatce jest pobierany dopiero po
  stuknięciu „Pokaż” albo automatycznie, jeśli to włączysz. Jego serwer widzi Twój adres IP;
  Brulion nie wysyła ciasteczek ani adresu strony odsyłającej.
* **Importy** (gdy je uruchomisz): import z Microsoft To Do lub OneNote, Todoist albo Airtable
  loguje się Twoim kontem albo tokenem, trzymanym tylko w pamięci na czas importu.
* **Sprawdzanie nowej wersji**: nie w aplikacji z Google Play. Aplikacja z GitHuba lub F-Droid i
  wersja na Windows pytają GitHuba (`api.github.com`) najwyżej raz dziennie, czy jest nowsze
  wydanie; GitHub widzi Twój adres IP, nic więcej nie jest wysyłane. Można to wyłączyć w
  ustawieniach.
* **Na komputerze** możesz też dać innym programom dostęp do notatnika — wierszowi poleceń
  `brulion-cli`, serwerowi MCP dla agentów AI, rozszerzeniu Brulion Web Clipper w przeglądarce,
  agentom AI udostępnionym telefonowi. Działają tylko wtedy, gdy je skonfigurujesz, a to, co te
  programy robią z notatkami, podlega ich własnym warunkom.

Adresy znane Brulionowi są połączone przez HTTPS. Jeśli podasz zwykły adres `http://` serwera,
Brulion ostrzeże, a to, co trafia na ten serwer, nie jest szyfrowane po drodze.

## Zgłaszanie odpowiedzi AI

Możesz zgłosić odpowiedź asystenta AI, która jest błędna, obraźliwa albo szkodliwa. Zgłoszenie
jest wstępnie wypełnione w aplikacji pocztowej albo na stronie nowego zgłoszenia (issue) na
GitHubie, gdzie widzisz wszystko, co zawiera (np. odpowiedź i Twój komentarz), i możesz to
zmienić. Trafia do twórcy tylko wtedy, gdy je wyślesz; zgłoszenie na GitHubie jest publiczne.
Odpowiedź możesz też oznaczyć albo ukryć w aplikacji, co zostaje na Twoim urządzeniu.

## Hasła, tokeny i rejestr zdarzeń

* Hasła, klucze API i tokeny, które podajesz, są przechowywane w prywatnej pamięci aplikacji na
  urządzeniu, poza notatkami. Nigdy nie są synchronizowane i są pomijane w kopii zapasowej Androida
  oraz przy przenoszeniu na nowy telefon.
* Brulion prowadzi na urządzeniu rejestr zdarzeń, który pomaga zrozumieć problemy (np. błąd
  synchronizacji). Może zawierać adresy serwerów, nazwy użytkowników i nazwy notatek, nigdy hasła,
  tokeny ani treść notatek. Opuszcza urządzenie tylko wtedy, gdy go udostępnisz albo zapiszesz.
* Szyfrowane notatki i blokada aplikacji (PIN albo blokada ekranu telefonu) są dostępne, jeśli ich
  chcesz; PIN jest przechowywany na urządzeniu tylko jako skrót (hash).

## Uprawnienia na Androidzie

| Uprawnienie | Do czego Brulion go używa |
|---|---|
| Internet, stan sieci | Opisane wyżej funkcje, które łączą się z serwerem; czekanie na sieć przed synchronizacją |
| Powiadomienia | Przypomnienia i powiadomienie o pracującym agencie AI |
| Alarmy i przypomnienia (dokładne alarmy) | Przypomnienia i alarmy co do minuty |
| Powiadomienia pełnoekranowe | Ekran alarmu na ekranie blokady |
| Uruchamianie przy starcie | Ponowne ustawienie przypomnień po restarcie telefonu |
| Wibracje | Przypomnienia i alarmy |
| Zapobieganie uśpieniu, usługa na pierwszym planie (synchronizacja danych) | Agent AI na Twoim komputerze odpowiadający przy wyłączonym ekranie |
| Biometria | Blokada aplikacji odciskiem palca, twarzą albo blokadą ekranu |
| Mikrofon | Notatki głosowe, nagrywane tylko w trakcie nagrywania; zapisywane w notatniku i przepisywane na tekst na urządzeniu |
| Zmiana ustawień dźwięku | Odtwarzanie i nagrywanie dźwięku przez właściwy głośnik albo słuchawki |

Brulion nie ma uprawnienia do aparatu: zdjęcie do czatu AI robi aplikacja aparatu. Dyktowanie w
czacie AI korzysta z aplikacji rozpoznawania mowy w telefonie, która obsługuje dźwięk na własnych
warunkach; Brulion dostaje tylko tekst. Notatki i pliki udostępnione Brulionowi przez inne
aplikacje („Udostępnij → Brulion”) i polecenia aplikacji do automatyzacji zostają na urządzeniu.
Aplikacja z GitHuba i F-Droid prosi też o „Dostęp do wszystkich plików” (folder notatnika w pamięci
współdzielonej) i ma uprawnienie „alarmy” przeznaczone dla budzików; aplikacja z Google Play nie
ma żadnego z nich.

## Dzieci

Brulion nie jest przeznaczony dla dzieci poniżej 13 lat.

## Usuwanie danych

Twórca nie przechowuje żadnych danych o Tobie, więc po jego stronie nie ma czego usuwać.
Odinstalowanie Brulionu usuwa jego pamięć prywatną: notatniki w niej trzymane, hasła i tokeny,
pobrane modele i rejestr zdarzeń. Wybrany przez Ciebie folder zostaje tam, gdzie jest. Dane wysłane na
Twoje serwery albo do wybranych usług (Twój Nextcloud, GitHub, dostawca AI) usuwa się u nich,
według ich zasad.

## Zmiany i kontakt

Zmiana tej polityki jest publikowana pod tym adresem z nową datą obowiązywania; każda wcześniejsza
wersja jest w historii pliku
[docs/PRIVACY.md](https://github.com/janusz-bit/brulion/commits/main/docs/PRIVACY.md). Pytania:
[zgłoszenie na GitHubie](https://github.com/janusz-bit/brulion/issues) albo adres e-mail twórcy
podany na stronie Brulionu w Google Play.
