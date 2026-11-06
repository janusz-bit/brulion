# Awaria produkcji 19.02

- 03:12 alert z Grafany: wszystkie requesty 502
- 03:25 dzwoni Marcin, loguję się przez VPN
- przyczyna: dysk na serwerze web-01 zapełniony logami nginx (100%), logrotate nie działał od miesiąca
- 03:50 wyczyszczone stare logi, restart nginx, strona wstała
- 04:30 napisane tymczasowe zadanie cron
- postmortem w piątek
- wniosek: monitoring miejsca na dysku z progiem 80%, a nie tylko CPU
#praca #incydent
