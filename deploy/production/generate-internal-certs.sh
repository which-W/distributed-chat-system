#!/bin/sh
# Run from repository root on the Linux server. Never overwrite an existing CA.
set -eu
umask 077
out=run/production/certs
if [ -d "$out" ] && [ -n "$(ls -A "$out")" ]; then
    echo "Certificate directory is not empty; preserve existing certificates." >&2
    exit 1
fi
mkdir -p "$out"
openssl req -x509 -newkey rsa:3072 -nodes -sha256 -days 3650 \
    -subj "/CN=deepecho-internal-ca" \
    -keyout "$out/ca.key" -out "$out/ca.crt"
for name in gate status chatserver1 varify; do
    openssl req -new -newkey rsa:2048 -nodes -subj "/CN=$name" \
        -keyout "$out/$name.key" -out "$out/$name.csr"
    printf 'subjectAltName=DNS:%s\nextendedKeyUsage=serverAuth,clientAuth\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n' "$name" > "$out/$name.ext"
    openssl x509 -req -in "$out/$name.csr" -CA "$out/ca.crt" \
        -CAkey "$out/ca.key" -CAcreateserial -days 365 -sha256 \
        -extfile "$out/$name.ext" -out "$out/$name.crt"
    rm "$out/$name.csr" "$out/$name.ext"
done
# Do not mount the CA signing key in running services.
mkdir -p run/production/ca-private
mv "$out/ca.key" run/production/ca-private/ca.key
echo "Internal certificates created. Back up run/production securely; renew leaf certificates before one year."
