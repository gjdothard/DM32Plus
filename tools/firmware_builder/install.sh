#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# Administrator setup: install.sh DEPLOYMENT PRODUCT_JSON COMPILER_IMAGE SITE_URL
set -eu
test "$(id -u)" = 0
test "$#" = 4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(realpath "$1")
case "$root" in /|'') echo 'Invalid deployment root' >&2; exit 1 ;; esac
test -d "$root/site"
grep -q CPS_BUILD_API "$root/serve.py"
grep -q _build_request "$root/server.py"
test -f /etc/website-updater/compose.yaml
image=$3
case "$image" in *@sha256:*) ;; *) echo 'Use a compiler image pinned by digest' >&2; exit 1 ;; esac
docker image inspect "$image" >/dev/null
exec 9> "$root/.deploy.lock"
flock -w 120 9
backup=$(mktemp -d /etc/website-updater/builder.previous.XXXXXXXX)
cp -a /etc/website-updater/compose.yaml "$backup/compose.yaml"
install -d -m 755 /usr/local/lib/firmware-builder
if test -f /usr/local/lib/firmware-builder/server.py; then
    cp -a /usr/local/lib/firmware-builder/. "$backup/"
fi
install -m 644 "$here/server.py" /usr/local/lib/firmware-builder/server.py
install -m 644 "$here/../release_version.py" /usr/local/lib/firmware-builder/release_version.py
install -m 644 "$2" /usr/local/lib/firmware-builder/product.json
install -d -o 65534 -g 65534 -m 700 /var/lib/firmware-builder
python3 - "$root" "$image" "$4" <<'PY'
import json,re,sys
from pathlib import Path
from urllib.parse import urlsplit
root,image,url=sys.argv[1:]
if not re.fullmatch(r"[A-Za-z0-9./:_-]+@sha256:[a-f0-9]{64}",image):
    raise SystemExit('Invalid pinned compiler image')
parsed=urlsplit(url)
if parsed.scheme!='https' or not parsed.hostname or parsed.username or parsed.query or parsed.fragment or parsed.path not in ('','/'):
    raise SystemExit('Use the public HTTPS site URL for the feature catalog')
path=Path('/etc/website-updater/compose.yaml')
config=json.loads(path.read_text())
cps=config['services']['cps']
cps.setdefault('environment',{})['CPS_BUILD_API']='http://firmware-builder:8081/v1/'
cps['environment']['CPS_BUILD_ORIGINS']=parsed.scheme+'://'+parsed.netloc
def mount(source,target):
    return dict(type='bind',source=source,target=target,read_only=True,bind={'create_host_path':False})
config['services']['firmware-builder']=dict(
    image=image,restart='unless-stopped',user='65534:65534',read_only=True,
    cpus=1,mem_limit='1200m',memswap_limit='1500m',pids_limit=96,
    cap_drop=['ALL'],security_opt=['no-new-privileges:true'],
    tmpfs=['/tmp:rw,nosuid,nodev,size=128m'],environment={'HOME':'/tmp'},
    command=['python3','-B','/app/firmware_builder/server.py',
        '--product','/app/firmware_builder/product.json','--site-url',url,
        '--state','/state','--compiler',image],
    volumes=[mount('/usr/local/lib/firmware-builder','/app/firmware_builder'),
        mount('/usr/local/lib/firmware-builder/release_version.py','/app/release_version.py'),
        dict(type='bind',source='/var/lib/firmware-builder',target='/state',bind={'create_host_path':False})],
    healthcheck=dict(test=['CMD','python3','-B','-c',
        "from urllib.request import urlopen;urlopen('http://127.0.0.1:8081/v1/health',timeout=3).close()"],
        interval='10s',timeout='5s',retries=3,start_period='15s'))
path.write_text(json.dumps(config,indent=2)+'\n')
PY
compose() { docker compose -f /etc/website-updater/compose.yaml "$@"; }
verify() {
    compose config --quiet &&
    compose up -d --force-recreate --wait --wait-timeout 90 firmware-builder cps &&
    compose exec -T cps python -B -c "import json;from urllib.request import urlopen;assert json.load(urlopen('http://127.0.0.1:8080/_firmware_build/v1/health',timeout=10))['status']=='ok'"
}
if ! verify; then
    compose stop firmware-builder || true
    cp -a "$backup/compose.yaml" /etc/website-updater/compose.yaml
    for file in server.py product.json release_version.py; do
        if test -f "$backup/$file"; then
            install -m 644 "$backup/$file" "/usr/local/lib/firmware-builder/$file"
        fi
    done
    if python3 -c "import json,sys;sys.exit(0 if 'firmware-builder' in json.load(open('/etc/website-updater/compose.yaml'))['services'] else 1)"; then
        compose up -d --force-recreate --wait --wait-timeout 90 firmware-builder cps
    else
        compose up -d --force-recreate --wait --wait-timeout 60 cps
    fi
    echo "Previous Compose definition restored; saved at $backup" >&2
    exit 1
fi
compose ps firmware-builder cps
printf 'Previous Compose definition retained at %s\n' "$backup"
