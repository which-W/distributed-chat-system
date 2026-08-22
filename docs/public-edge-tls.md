# Public HTTPS and Chat TLS deployment

Production traffic uses two independent encrypted public paths:

```text
Qt client -- HTTPS --> Nginx --> 127.0.0.1:8080 Gate
Qt client -- TLS TCP -> Nginx --> 127.0.0.1:8989 Chat
Gate/Status/Chat -- gRPC mTLS --> private service network
```

Nginx terminates public TLS on the same host as each backend. Plain HTTP/TCP is
therefore restricted to the loopback interface. MySQL, Redis, Status, Verify and
peer gRPC ports must remain private.

## Production endpoint rules

- Gate uses `api.chat.example.com:443`; its backend uses `127.0.0.1:8080`.
- Each Chat host uses its own DNS name and public TLS port 443; its backend uses
  `127.0.0.1:8989`.
- On a one-machine test deployment, use 443 for Gate, 8443 for Chat 1 and 8444
  for Chat 2 because HTTP and Stream listeners cannot share the same address and
  port.
- Status returns `PublicHost`, `PublicPort`, `Transport=tls` and `TLSName`. Never
  return `0.0.0.0`, a loopback address or a private address to Internet clients.
- Production clients set `AllowInsecure=false`. There is no automatic fallback
  from TLS to plaintext.

## Install and validate Nginx

Copy `deploy/nginx/gate-http.conf.example` to the Gate host's HTTP include
directory. Copy `deploy/nginx/chat-stream.conf.example` to each Chat host and
replace the example domain. Stream configuration is a top-level block, not a
child of `http {}`.

Check Stream TLS support and configuration before reload:

```bash
nginx -V 2>&1 | grep stream
sudo nginx -t
sudo systemctl reload nginx
```

The build flags must include `--with-stream` and `--with-stream_ssl_module`, or
the distribution must load the equivalent dynamic module.

## Certificates

Use public CA certificates for desktop clients. Example Certbot commands:

```bash
sudo certbot --nginx -d api.chat.example.com
sudo certbot certonly --standalone -d chat1.chat.example.com
sudo certbot renew --deploy-hook "nginx -t && systemctl reload nginx"
```

Never commit private keys or live certificates. The repository ignores common
certificate and key extensions.

## Firewall policy

Gate accepts public 80/443 only. Chat accepts its public TLS port only. Backend
8080/8989 must be bound to loopback. Ports 5000, 5050, 50055/50056, 3306 and
6379 are restricted to the service network and protected by host firewall or
cloud security groups.

## Verification

```bash
curl --fail --show-error https://api.chat.example.com/
openssl s_client -connect chat1.chat.example.com:443 \
  -servername chat1.chat.example.com -verify_return_error
sudo certbot renew --dry-run
```

Also verify that a wrong hostname, expired/untrusted certificate and plaintext
Chat endpoint are all rejected by the desktop client.

On Windows, run the generated TLS runtime probe before distributing the client:

```powershell
.\build\desktop-qt6-local\bin\chat_tls_probe.exe
.\build\desktop-qt6-local\bin\chat_tls_probe.exe api.chat.example.com 443
```

`TLS available` must be `yes`. Qt 5.12 on Windows needs matching OpenSSL runtime
DLLs that `windeployqt` does not supply. Do not solve this by bundling an obsolete
OpenSSL release; use a maintained Qt 6 Windows kit for production packaging.
