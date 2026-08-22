#!/usr/bin/env sh
set -eu

output_dir="${1:-certs}"
mkdir -p "$output_dir"

openssl genrsa -out "$output_dir/ca.key" 4096
openssl req -x509 -new -key "$output_dir/ca.key" -sha256 -days 3650 \
  -subj "/CN=distributed-chat-dev-ca" -out "$output_dir/ca.crt"

for name in gate status chatserver1 chatserver2 varify; do
  openssl genrsa -out "$output_dir/$name.key" 2048
  openssl req -new -key "$output_dir/$name.key" -subj "/CN=$name" \
    -out "$output_dir/$name.csr"
  printf 'subjectAltName=DNS:%s,DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth,clientAuth\n' "$name" \
    > "$output_dir/$name.ext"
  openssl x509 -req -in "$output_dir/$name.csr" -CA "$output_dir/ca.crt" \
    -CAkey "$output_dir/ca.key" -CAcreateserial -out "$output_dir/$name.crt" \
    -days 825 -sha256 -extfile "$output_dir/$name.ext"
  rm -f "$output_dir/$name.csr" "$output_dir/$name.ext"
done

chmod 600 "$output_dir"/*.key
printf 'Development certificates generated in %s\n' "$output_dir"
