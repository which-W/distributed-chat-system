#!/bin/sh
# Add the Resource Server identity to an existing installation without replacing its CA.
set -eu
umask 077
out=run/production/certs
ca_key=run/production/ca-private/ca.key
test -s "$out/ca.crt" && test -s "$ca_key" || {
    echo "Existing internal CA certificate and signing key are required." >&2
    exit 1
}
if [ -e "$out/resource.crt" ] || [ -e "$out/resource.key" ]; then
    echo "Resource certificate/key already exists; refusing to overwrite." >&2
    exit 1
fi
work=$(mktemp -d "$out/.resource-cert.XXXXXX")
trap 'rm -f "$work/resource.key" "$work/resource.csr" "$work/resource.ext" "$work/resource.crt"; rmdir "$work"' EXIT HUP INT TERM
openssl req -new -newkey rsa:2048 -nodes -subj "/CN=resource1" \
    -keyout "$work/resource.key" -out "$work/resource.csr"
printf 'subjectAltName=DNS:resource1,DNS:resource2\nextendedKeyUsage=serverAuth,clientAuth\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n' > "$work/resource.ext"
openssl x509 -req -in "$work/resource.csr" -CA "$out/ca.crt" \
    -CAkey "$ca_key" -set_serial "0x$(openssl rand -hex 16)" -days 365 -sha256 \
    -extfile "$work/resource.ext" -out "$work/resource.crt"
openssl verify -CAfile "$out/ca.crt" "$work/resource.crt"
mv "$work/resource.key" "$out/resource.key"
mv "$work/resource.crt" "$out/resource.crt"
echo "Resource certificate created with the existing internal CA."
