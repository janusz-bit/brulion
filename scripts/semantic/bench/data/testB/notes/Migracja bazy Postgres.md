# Migracja bazy Postgres 13 → 16

Plan na sobotnią noc (przerwa serwisowa 2h).

1. pg_dump całej bazy `crm_prod` (ok. 48 GB, trwa około 40 min)
2. postawić nową instancję na serwerze db-02
3. pg_restore z `--jobs 8`
4. porównać liczby wierszy w największych tabelach
5. przełączyć connection string w aplikacji
6. zostawić stary serwer przez tydzień jako zapas

Ryzyka: rozszerzenie `pg_trgm` trzeba doinstalować, sekwencje nie zawsze się przenoszą, sprawdzić `ANALYZE` po restore.
- [ ] test na kopii w środę
- [x] poinformować support o oknie serwisowym
#praca #baza
