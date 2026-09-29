#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
server_ip="${1:?Usage: bash create_certificate.sh YOUR_EXTERNAL_IPV4}"
python3 - "$server_ip" <<'PY'
import ipaddress, sys
ipaddress.IPv4Address(sys.argv[1])
PY
if [[ -e server.key || -e server.crt ]]; then
    echo 'Certificate files already exist. Keep them; do not regenerate on each build.' >&2
    exit 1
fi
umask 077
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 365 \
    -keyout server.key -out server.crt -subj '/CN=JengChat server' \
    -addext "subjectAltName=IP:${server_ip}" \
    -addext 'basicConstraints=critical,CA:FALSE' \
    -addext 'keyUsage=critical,digitalSignature,keyEncipherment' \
    -addext 'extendedKeyUsage=serverAuth'
chmod 600 server.key
chmod 644 server.crt
openssl x509 -in server.crt -noout -fingerprint -sha256 -dates
echo 'Copy only server.crt into the client assets/security/ folder. Keep server.key on this VM.'
