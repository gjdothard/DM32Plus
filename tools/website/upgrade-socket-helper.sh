#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# One-time administrator migration from the original sudo helper.
set -eux
[ "$(id -u)" = 0 ] || exit 1
here=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
test -s /etc/website-updater/config.json
test "$(id -Gn website-updater)" = website-updater
test -f /etc/sudoers.d/website-updater
test ! -e /etc/systemd/system/website-deploy.socket
test ! -e /etc/systemd/system/website-deploy@.service
systemctl stop website-release.timer website-release.service
backup=$(mktemp -d /var/lib/website-deployer/socket-upgrade.previous.XXXXXXXX)
for name in update.py privileged.py; do
    cp -a "/usr/local/lib/website-updater/$name" "$backup/$name"
    install -m 644 "$here/$name" "/usr/local/lib/website-updater/$name"
done
cp -a /etc/sudoers.d/website-updater "$backup/sudoers"
cp -a /usr/local/sbin/website-deploy "$backup/website-deploy"
cp -a /etc/systemd/system/website-release.service "$backup/website-release.service"
install -m 644 "$here/website-deploy.socket" /etc/systemd/system/website-deploy.socket
install -m 644 "$here/website-deploy@.service" /etc/systemd/system/website-deploy@.service
mkdir -p /etc/systemd/system/website-release.service.d
cat > /etc/systemd/system/website-release.service.d/socket-helper.conf <<'EOF'
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
[Unit]
Requires=website-deploy.socket
After=website-deploy.socket
[Service]
NoNewPrivileges=true
EOF
rm -- /etc/sudoers.d/website-updater /usr/local/sbin/website-deploy
systemctl daemon-reload
systemctl enable --now website-deploy.socket
systemctl reset-failed website-release.service
systemctl start website-release.timer
systemctl start website-release.service
systemctl status website-deploy.socket website-release.timer --no-pager
printf 'Original helper files retained at %s\n' "$backup"
