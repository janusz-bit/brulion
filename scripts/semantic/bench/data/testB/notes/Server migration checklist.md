# Server migration checklist

Moving the old VPS (Debian 10) to the new Hetzner box before the end of the month.

- [x] order the new server, 8 cores, 32 GB RAM
- [x] set up SSH keys, disable password login
- [ ] copy /var/www and /etc/nginx with rsync
- [ ] lower DNS TTL to 300 seconds one day before the switch
- [ ] check cron jobs and systemd timers on the old machine
- [ ] test mail relay (SPF/DKIM)
- final step: change the A record, watch the logs for 24h

Note to self: forgot about the Let's Encrypt hook last time. Don't forget it again.
#work #sysadmin
