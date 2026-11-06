# Deployment na staging

- wdrożenie wersji 2.14 na staging w czwartek wieczorem, okno 20:00–21:00
- pipeline w GitLab CI, job `deploy-staging` czasem się wiesza na kroku helm upgrade
- [[Klaster Kubernetes]] — namespace `app-stg`, 3 repliki
- smoke testy: logowanie, eksport PDF, płatności sandbox
- [ ] poprosić Marcina o reset bazy staging przed deploymentem
- [x] zaktualizować zmienne środowiskowe w Vault
- jak coś pójdzie źle: `helm rollback app 1`, nie dotykać produkcji
#praca #devops
