# Raspberry Pi - Pi-hole w domu

Stary RPi 3B+ z szuflady jako serwer DNS z blokadą reklam.

- Instalacja: Raspberry Pi OS Lite 64-bit, zapisane przez Imager na karcie 16 GB, SSH włączony, użytkownik "pi" zmieniony
- `curl -sSL https://install.pi-hole.net | bash` - spojrzałem w skrypt przed uruchomieniem
- Statyczny IP 192.168.0.40, w routerze DNS ustawiony na Pi
- Listy blokujące: StevenBlack + Hagezi Pro, ok. 480 tys. domen
- Whitelist: allegro.pl cdn, bo reklamy z kampanii potrzebne do kuponów, oraz Netflix
- Efekt: 31% zapytań blokowane, strony ładują się szybciej, ale YouTube nadal ma reklamy
- Zasilacz: oryginalny 5V/2,5A, przy słabszym "undervoltage"

Backup konfiguracji: Teleporter raz w miesiącu.

- [ ] unbound zamiast Cloudflare jako upstream

#sprzęt #linux
