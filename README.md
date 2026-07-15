# wirebard

WireGuard config manager & compiler. Describe each network once in a folder of
*partials*, and wirebard substitutes variables, checks the address plan, and
compiles everything into a per-interface `/etc/wireguard/<network>.conf` —
installed 0600 and reloaded live with `wg syncconf` so connected peers never
drop. It also speaks a small JSON contract over SSH, so a machine caller can
add and remove peers while wirebard owns every policy decision — address,
AllowedIPs, DNS, MTU. That is all it does.

Also: a C++23 learning project. The philosophy — do exactly one job, own no
orchestration, generate no artifact a human could write. The source carries
`C++ LESSON:` teaching comments aimed at someone coming from Node.js/Go.

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/vexdynamics/wirebard/main/install.sh | sh
```

Detects the architecture (x86_64 / aarch64 / armhf / armel), downloads the
latest release, verifies it against `SHA256SUMS`, and installs a single static
binary to `/usr/local/bin` (override with `WIREBARD_BIN_DIR=...`). Or grab a
tarball straight from the release with `gh`:

```bash
gh release download -R vexdynamics/wirebard -p "*linux-$(uname -m)*" && tar -xzf wirebard-*.tar.gz
```

## Requirements

gcc 14+ (C++23), cmake 3.28+, ninja. No third-party libraries — the tool is
dependency-free by design. On the **server**, `apply` (and a live `build` of a
running interface) shells out to `wg`, `wg-quick`, and `systemctl`, so
`wireguard-tools` + systemd must be present there; the pure commands
(`check` / `build` to a file / `network list` / `peer … --dry-run`) need
neither. No toolchain on the host? See "Build in docker" below.

## Build

```bash
cmake --preset debug            # configure (once; ASan+UBSan on)
cmake --build --preset debug    # compile
ctest --preset debug            # run the test suite
./build/debug/src/wirebard --help
```

`cmake --preset release` / `--build --preset release` for the optimized binary,
or `--preset release-static` for the fully static one that release tarballs ship
(runs on any Linux of that arch, no library versions to match).

## Releases

Push a tag and CI does the rest — `.github/workflows/release.yml` builds the
static binary, runs the full suite against it, and publishes a GitHub Release
with tarballs (x86_64, aarch64, armhf, armel — every Pi back to the original)
and `SHA256SUMS`:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

`wirebard --version` reports the baked-in tag (`dev` for local builds).
Deploying a release to a server is `tar -xzf` + move one file — the binary has
zero runtime dependencies beyond `wg`/`wg-quick`/`systemctl` themselves.
Reproduce the x86_64 artifact locally:

```bash
cmake --preset release-static -DWIREBARD_VERSION=v1.0.0
cmake --build --preset release-static
```

Every push/PR runs `.github/workflows/ci.yml`: the suite under ASan+UBSan plus
the clean-room `docker build`.

### Build in docker

`Dockerfile` and `compose.yaml` are hand-written infrastructure — wirebard
itself knows nothing about them:

```bash
docker build -t wirebard .        # clean-room compile + full ctest suite
docker run --rm wirebard --help   # the built binary
docker compose run --rm ci        # just the test suite
```

## A wirebard project

A project directory mirrors `/etc/wireguard` on a real server — back up or
rsync the directory, run `wirebard build` on the other side, and the server is
recreated. WireGuard is one file per interface, so each **network** is a
subfolder of partials that compiles to its own `<network>.conf`:

```
/etc/wireguard/                    ↔  project root (default; -C to override)
├── backups.conf                      COMPILED output — wg-quick@backups reads this
├── roam.conf                         COMPILED output — wg-quick@roam reads this
└── partials/
    ├── backups/                      one folder per network (= per interface)
    │   ├── server.key                the private key: 0600, gitignored, NEVER emitted
    │   ├── template.conf             copy-me reference; NOT compiled
    │   ├── 00-main.conf              [Interface] + every #= variable
    │   ├── 10-alice.conf             one [Peer] per file...
    │   └── 20-web01.conf             ...merged in FILENAME ORDER
    └── roam/
        ├── 00-main.conf              tunnel = full (road-warrior)
        └── 10-laptop.conf
```

`--network backups` ⇒ folder `partials/backups/` ⇒ compiled `backups.conf` ⇒
systemd unit `wg-quick@backups`. **Folder name = network = interface.** Only
`*.conf` except `template.conf` compiles, in bytewise filename order — hence the
`NN-` prefix: `00-main.conf` first, then one peer per file. `peer add` writes
the next `NN-<name>.conf` for you; you can also hand-copy `template.conf`. The
**peer partials are the ledger** — a peer's assigned address is its
server-side `AllowedIPs` /32, so there is no side-channel state file to drift.

## Variables & partials

A partial is plain WireGuard syntax plus `${variable}` placeholders. Values live
on **directive lines** — comments with the wirebard sigil, invisible to
WireGuard. All variables are defined in ONE place: `00-main.conf` (the file that
sorts first), and they work in every partial in that network:

```ini
# 00-main.conf — the only home for variables, and the [Interface] stanza
#= tunnel = split                 # split | full  (the ONLY thing that differs
#                                 #   between network kinds — see below)
#= subnet = 10.8.2.0/24           # the pool wirebard allocates peer /32s from
#= address = 10.8.2.1/24          # the server's own address in the subnet
#= endpoint = vpn.example.com:51820
#= dns = 10.8.2.1
#= mtu = 1420
#= server_public_key = <base64>   # public; emitted into client configs
#= prod: endpoint = vpn.prod.example.com:51820   # override for --env prod

[Interface]
Address    = ${address}
ListenPort = 51820
# No PrivateKey here — wirebard injects it into the compiled 0600 conf from
# server.key. Generate the pair: wg genkey | tee server.key | wg pubkey
```

- `#= name = value` — the value runs to end of line: no quotes, no types, no
  trailing comments. A later line wins for the same key; `--env NAME` lays every
  `#= NAME: ...` line on top.
- Loud failure over silent surprise: a `#=` line in any other partial is an
  error, and so is asking for an env that no `#= NAME: ...` line mentions.
- The sigil needs a following space: `#=====` divider art stays a comment.
- Escape a literal with `$${not_a_var}`.
- `tunnel` decides split vs full only in the **client** config wirebard renders
  (split → reach the subnet; full → `AllowedIPs = 0.0.0.0/0, ::/0`). The server
  side is identical either way — a full tunnel just needs your own NAT `PostUp`
  in `00-main.conf` (wirebard synthesizes no iptables).

`partials/<network>/template.conf` documents the peer format; copy it to add a
peer by hand.

## Commands

You rarely need `-C`: wirebard uses the current directory if it has a
`partials/` subdir, and falls back to `/etc/wireguard` — so on the server every
command just works from anywhere. With no `[network]`, `check`/`build`/`apply`
act on every network.

```bash
wirebard check [network]     # address plan: peers in-subnet, no dup pubkey/
                             #   address, no collision with the server address
wirebard build [network]     # compile → /etc/wireguard/<network>.conf (0600),
                             #   validated first; does NOT touch the interface
wirebard apply [network]     # build, then install + reload live (needs sudo)
wirebard apply --dry-run     # print the exact wg/systemctl plan; touch nothing
wirebard list                # networks and their peers, human-readable
wirebard network list --json # machine-readable inventory (JSON contract)
wirebard peer add    --network N --pubkey BASE64 --name LABEL [--json] [--dry-run]
wirebard peer remove --network N --pubkey BASE64 [--json] [--dry-run]
```

`apply` reloads without dropping peers: it writes the 0600 conf,
`systemctl enable`s the unit (persist across reboot), then `wg syncconf`
reconciles the live interface (or `systemctl start` if it isn't up yet).
Exit codes: `0` ok · `1` config/validation failure · `2` usage · `3`
environment (no project, `wg`/`systemctl` missing, apply failed). Add
`--verbose` to echo every external command, copy-pasteable.

## The machine contract

`peer add` / `peer remove` / `network list` are the imperative, JSON-speaking
interface a machine caller drives over SSH. wirebard owns every policy decision;
the caller only generates its keypair locally and fills the returned config.

```bash
wirebard peer add --network backups --pubkey <PUB> --name web01 --json
# {"network":"backups","type":"isolated","address":"10.8.2.3/24",
#  "server_public_key":"...","endpoint":"vpn.example.com:51820",
#  "client_config":"[Interface]\nPrivateKey = {{PRIVATE_KEY}}\n..."}
```

- **stdout is exactly one JSON object**; all logs go to stderr.
- **Idempotent** by pubkey: re-adding returns the same address, never a
  duplicate. `peer remove` on an unknown pubkey returns `{"removed": false}`.
- The `client_config` carries a single `{{PRIVATE_KEY}}` placeholder — the
  caller substitutes its own key; wirebard never sees a private key.
- `peer add` is sugar over the declarative spine: author a peer partial →
  build → check → apply, all under a per-network lock, with the partial rolled
  back if apply fails. A malformed `--pubkey` is rejected up front (exit 2).

The full contract and design are in `docs/design/peer-provisioning.md`.

## Try it

The repo ships working sample partials (two networks — a split-tunnel and a
full-tunnel one). No server key needed for the read-only and dry-run paths:

```bash
wirebard -C samples check                 # both networks validate
wirebard -C samples network list --json   # [{"name":"backups","type":"isolated",...},...]
wirebard -C samples peer add --network backups \
    --pubkey AbCdEfGhIjKlMnOpQrStUvWxYz0123456789+/ABCDE= --name demo --dry-run --json
#   → allocates 10.8.2.4/24 (past the server .1 and the two sample peers)
```

## The server workflow

On the box, `/etc/wireguard` IS the project (`git init` it — versioning that one
directory versions the server; `server.key` files are gitignored):

```bash
# one-time per network: the keypair (private stays 0600, public goes in the var)
wg genkey | tee /etc/wireguard/partials/backups/server.key | wg pubkey
# then edit 00-main.conf's server_public_key, and:
sudo wirebard apply backups
```

Peers arrive over the contract (`wirebard peer add … --json` via SSH) or by hand
(`cp template.conf 30-newpeer.conf`, then `sudo wirebard apply backups`). Nothing
touches the live interface until the next `apply` — inspect with
`wirebard build` + `wirebard check` first.

## Development notes

- Debug preset compiles with AddressSanitizer + UBSan — lifetime and memory
  bugs crash loudly in tests instead of corrupting silently. Keep it on.
- `clang-format -i src/*.cpp src/*.h src/commands/*.cpp tests/*.cpp` before
  committing; `clang-tidy -p build/debug src/*.cpp` for the linter.
- Architecture: a pure core (directives/vars/partial/net/peer/alloc/render/
  check/json/contract — all unit-tested without I/O), thin command shells, and
  the OS-facing modules (fs, subprocess, apply) with RAII ownership over every
  fd, lock, and temp dir.
- House rules live in CLAUDE.md; the conventions (errors as values, pure core
  vs OS boundary vs thin commands, determinism, loud failure, secrets never in
  logs) are non-negotiable.

## License

[MIT](LICENSE) © 2026 vexdynamics
