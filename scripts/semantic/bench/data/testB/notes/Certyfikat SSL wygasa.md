# Certyfikat SSL

Certyfikat dla api.firma-demo.pl wygasa **28 listopada**. Let's Encrypt nie odnowił się automatycznie, bo certbot na serwerze web-02 ma zły cron.

- sprawdzić `certbot renew --dry-run`
- port 80 musi być otwarty dla challenge
- [ ] dodać alert na 14 dni przed wygaśnięciem
- [ ] rozważyć wildcard przez DNS-01
Poprzednio to samo zdarzyło się w maju, wtedy klient zauważył pierwszy. Wstyd.
#praca #ssl
