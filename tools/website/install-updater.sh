#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# One-time administrator setup: install-updater.sh /opt/DEPLOYMENT PRODUCT_JSON
set -eux
[ "$(id -u)" = 0 ] || { echo "run this reviewed installer as root" >&2; exit 1; }
root=$(realpath "$1")
case $root in /opt/*) ;; *) echo "expected a deployment under /opt" >&2; exit 2 ;; esac
case ${root#/opt/} in ''|*[!A-Za-z0-9_-]*) echo "invalid deployment directory" >&2; exit 2 ;; esac
here=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
test -s "$root/compose.yaml"
test -d "$root/site"
for path in /etc/website-updater /usr/local/lib/website-updater /usr/local/sbin/website-deploy /etc/sudoers.d/website-updater /etc/systemd/system/website-release.service /etc/systemd/system/website-release.timer /var/lib/website-deployer /etc/systemd/system/website-deploy.socket /etc/systemd/system/website-deploy@.service; do
    [ ! -e "$path" ] || { echo "refusing to overwrite existing path: $path" >&2; exit 1; }
done
# Refuse to silently reuse an account that may already have broader permissions.
if getent passwd website-updater >/dev/null; then
    echo "website-updater already exists; review its identity before updating this installation" >&2
    exit 1
fi
useradd --system --user-group --create-home --home-dir /var/lib/website-updater --shell /usr/sbin/nologin website-updater
chmod 700 /var/lib/website-updater
install -d -m 755 /etc/website-updater /usr/local/lib/website-updater
install -d -m 700 /var/lib/website-deployer
for file in update.py privileged.py deploy.sh; do
    install -m 644 "$here/$file" "/usr/local/lib/website-updater/$file"
done
install -m 644 "$here/../release_version.py" /usr/local/lib/website-updater/release_version.py
# Capture the current project's bindings once; the updater cannot change this file.
docker compose --project-directory "$root" config --format json | python3 -c '
import json, pathlib, re, sys
current = json.load(sys.stdin)
root, product_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
product = json.loads(product_path.read_text())
repo = product["repository"]
if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
    raise SystemExit("invalid repository")
name = current["name"]
if not re.fullmatch(r"[a-z0-9][a-z0-9_-]+", name):
    raise SystemExit("invalid project name")
ports = current["services"]["cps"]["ports"]
if any(p.get("target") != 8080 or p.get("protocol", "tcp") != "tcp" for p in ports):
    raise SystemExit("review nonstandard website ports before installation")
files = ("site", "serve.py", "server.py", "release_version.py", "release-products.json")
service = dict(image="python:3.12-slim", restart="unless-stopped", user="65534:65534", read_only=True,
    command=["python", "-B", "-u", "/app/serve.py"], cap_drop=["ALL"], security_opt=["no-new-privileges:true"],
    environment={"CPS_PUBLIC_HOST": current["services"]["cps"].get("environment", {}).get("CPS_PUBLIC_HOST", "")},
    ports=ports, volumes=[dict(type="bind", source=str(root/f), target="/app/"+f, read_only=True,
        bind={"create_host_path": False}) for f in files],
    healthcheck=dict(test=["CMD", "python", "-B", "-c",
        "from urllib.request import urlopen; urlopen(\"http://127.0.0.1:8080/codeplug/\", timeout=3).close()"],
        interval="10s", timeout="5s", retries=3, start_period="5s"))
base = pathlib.Path("/etc/website-updater")
(base/"compose.yaml").write_text(json.dumps({"x-license":"MIT",
    "x-copyright":"Copyright (c) 2026 Garrett Dothard", "name":name, "services":{"cps":service}}, indent=2)+"\n")
(base/"config.json").write_text(json.dumps({"_license":"MIT",
    "_copyright":"Copyright (c) 2026 Garrett Dothard", "repository":repo, "deployment_root":str(root)}, indent=2)+"\n")
' "$root" "$2"
chmod 644 /etc/website-updater/compose.yaml /etc/website-updater/config.json
install -m 644 "$here/website-deploy.socket" /etc/systemd/system/website-deploy.socket
install -m 644 "$here/website-deploy@.service" /etc/systemd/system/website-deploy@.service
cat > /etc/systemd/system/website-release.service <<'EOF'
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
[Unit]
Description=Fetch the latest stable website release
After=network-online.target website-deploy.socket
Wants=network-online.target
Requires=website-deploy.socket
[Service]
Type=oneshot
User=website-updater
Group=website-updater
WorkingDirectory=/var/lib/website-updater
ExecStart=/usr/bin/python3 /usr/local/lib/website-updater/update.py
TimeoutStartSec=300
UMask=0077
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
EOF
cat > /etc/systemd/system/website-release.timer <<'EOF'
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
[Unit]
Description=Check for stable website releases every two minutes
[Timer]
OnCalendar=*-*-* *:0/2:00
RandomizedDelaySec=10
Persistent=true
[Install]
WantedBy=timers.target
EOF
systemctl daemon-reload
systemctl enable --now website-deploy.socket
systemctl enable --now website-release.timer
systemctl start website-release.service
systemctl status website-release.timer --no-pager
id website-updater
passwd -S website-updater
test "$(id -Gn website-updater)" = website-updater
for path in /etc/website-updater/config.json "$root"; do
    if runuser -u website-updater -- test -w "$path"; then
        echo "updater unexpectedly has write access: $path" >&2; exit 1
    fi
done
if runuser -u website-updater -- test -r /var/run/docker.sock; then
    echo "updater unexpectedly has Docker socket access" >&2; exit 1
fi
# A successful policy query can exit zero even when every command is denied.
permissions=$(LC_ALL=C sudo -l -U website-updater 2>&1)
printf '%s\n' "$permissions"
case $permissions in
*"is not allowed to run sudo"*) ;;
*) echo "updater unexpectedly has sudo permissions" >&2; exit 1 ;;
esac
