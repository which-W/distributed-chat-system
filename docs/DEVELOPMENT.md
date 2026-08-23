# Development baseline

## Fresh builds

All supported configure commands use CMake's `--fresh` mode. This discards the
existing `CMakeCache.txt`, so paths from another machine or toolchain are never
reused.

Windows server build from a Developer Command Prompt:

```powershell
scripts\build-windows-server.cmd
```

Other presets:

```powershell
pwsh scripts/configure-fresh.ps1 -Preset desktop-release
```

```sh
sh scripts/configure-fresh.sh linux-server-release
```

## Tests

CTest includes the C++ password hashing test and, when Node is available, the
VarifyServer test suite:

```sh
ctest --test-dir build/linux-server-release --output-on-failure
cd VarifyServer
npm ci
npm run deps:check
npm run check
npm test
```

## MySQL and Redis for local development

The Compose credentials are intentionally limited to local development. Do not
reuse them in a shared or production environment.

```sh
cp .env.compose.example .env
docker compose up -d
docker compose ps
```

MySQL initializes from `database/schema.sql`. Redis enables AOF persistence and
password authentication. To rerun database initialization from an empty local
volume, use `docker compose down -v` only after confirming that the development
data can be deleted.

## Static analysis and warnings

Normal builds enable `/W4 /permissive-` on MSVC and
`-Wall -Wextra -Wpedantic -Wshadow` on GCC/Clang. Use these opt-in switches while
retiring existing warnings:

```sh
cmake --fresh --preset linux-server-release \
  -DCHAT_ENABLE_CLANG_TIDY=ON \
  -DCHAT_WARNINGS_AS_ERRORS=ON
```

Formatting and analysis rules live in `.clang-format` and `.clang-tidy`.

## Security behavior

- New passwords are stored as libsodium Argon2id hashes. Gate migrates legacy
  plaintext rows at startup using conditional updates; login also performs a
  fallback upgrade. Remove the compatibility path after confirming that no
  plaintext rows remain.
- Verification codes are cryptographically generated six-digit values, expire
  after five minutes, have a one-minute send cooldown and hourly issue limit, and
  are atomically consumed. Five wrong attempts invalidate a code.
- Status issues a random, server-bound chat ticket with a 60-second TTL. Chat
  consumes it atomically, so it cannot be replayed.
- Authenticated chat handlers derive the actor UID from the server-side session;
  request `uid` and `fromuid` fields are not authorization inputs.

The CI workflow covers Linux Server Release, Windows Client Release, CTest, Node
dependency/syntax/tests, and formatting checks.
