# Wymiana SSD w laptopie

Lenovo IdeaPad 3 żony, dysk HDD 1 TB 5400 rpm - Windows startuje 4 minuty.

- Kupiony: Kingston NV2 1 TB NVMe, 289 zł (Morele); w IdeaPadzie slot to M.2 SATA czy NVMe? sprawdzić!! (okazało się: NVMe, uff)
- Klonowanie: Macrium Reflect Free albo `dd` z Live USB; użyłem Clonezilli
- Rozkręcić: 11 śrubek Phillips, plastikowa łopatka, odpiąć baterię wewnętrzną
- Po klonowaniu: w BIOS tryb AHCI, rozszerzyć partycję C: w Zarządzaniu dyskami
- Stary dysk do obudowy USB 2,5" na backup
- TRIM sprawdzić: `fsutil behavior query DisableDeleteNotify`

Efekt: start systemu 18 s. Żona zadowolona, pokazała mamie.

- [x] przenieść licencję Office, wylogować się ze starego

#sprzęt #naprawy
