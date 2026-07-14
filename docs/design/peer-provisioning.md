# Peer provisioning — domain model & the baki CLI contract

**Status:** proposal, not yet implemented. This is the design that turns
wirebard's four stub commands into a real tool, driven by the first concrete
consumer: baki, which calls wirebard over SSH to add/remove WireGuard peers.

It settles the questions CLAUDE.md left open — the source format, how
fragments compose into `/etc/wireguard/wgN.conf`, and what each command
really does — by designing them against baki's contract sketch. Read the
contract sketch (in the conversation that spawned this) alongside this doc;
section 7 maps wirebard's output onto it field by field.

---

## 0. The one decision that shapes everything

baki wants an **imperative, mutating** API: `wirebard peer add … --json`
that assigns an address, applies it live, persists it, and returns the
result. wirebard's philosophy is a **declarative compiler**: fragments in a
source tree that `build`/`check`/`apply` render into the live config, with
the source tree mirroring the system so a backup recreates the server.

These reconcile if — and only if — `peer add` is **sugar over the pipeline,
not a second write path**:

```
peer add  →  author (or find) a peer fragment in the source tree   [the mutation]
          →  run build → check → apply on that network              [the spine, unchanged]
          →  render the client config + emit the JSON envelope      [the response]
```

The fragment on disk stays the single source of truth. `peer add` is the
only thing that *writes* a fragment programmatically; everything downstream
is the same deterministic compile any hand-edit would trigger. This is why
baki's Option C (drop a fragment over SSH) was rightly rejected as the
*interface* while remaining wirebard's *internal model*: baki gets a clean
imperative call, wirebard stays a compiler underneath.

Everything below follows from holding that line.

---

## 1. Filesystem layout — haladin's partials model, ported

This reuses haladin's `wishes/` structure almost verbatim. The one adaptation
is that WireGuard is **one file per interface**, so where haladin has a single
flat `wishes/` → one `haproxy.cfg`, wirebard has **one partials subfolder per
network** → one `<network>.conf` each. See `samples/` for working examples.

```
<project>/                         ↔  /etc/wireguard/  (the DEFAULT root)
├── backups.conf                      COMPILED output; wg-quick@backups reads this
├── roam.conf                         COMPILED output; wg-quick@roam reads this
└── partials/
    ├── backups/                      one folder per network (= per interface)
    │   ├── server.key                server private key: 0600, GITIGNORED, never emitted
    │   ├── template.conf             copy-me reference; NOT compiled
    │   ├── 00-main.conf              [Interface] + all #= variables (§2)
    │   ├── 10-alice.conf             one [Peer] per file, merged in NN- order (§3)
    │   └── 20-baki-web01.conf
    └── roam/
        ├── 00-main.conf              tunnel = full (§4)
        └── 10-laptop.conf
```

- `--network backups` ⇒ folder `partials/backups/` ⇒ compiled `backups.conf`
  ⇒ systemd unit `wg-quick@backups`. **Folder name = network = interface.**
- Only `*.conf` except `template.conf` is compiled; wirebard writes only peer
  partials (`NN-<name>.conf`). `00-main.conf`, `template.conf`, and
  `server.key` are hand-owned — "generate no artifact a human could write."

**Project-path resolution (settled):** default to the WireGuard config folder,
override for odd locations — exactly what you asked for. Explicit `-C <dir>`
wins; else the current directory if it contains `partials/` (dev workflow);
else `/etc/wireguard`. Ported from haladin's `resolve_project_root`, swapping
`/etc/haproxy` → `/etc/wireguard` and `wishes/` → `partials/`.

---

## 2. `00-main.conf` — the interface stanza + variables (hand-written)

Sorts first in its folder, so — exactly like haladin's `00-defaults.wish.conf`
— it owns the `[Interface]` stanza *and* is the single place every `#=`
variable is declared. The **`vars` module is reused nearly verbatim from
haladin**: `#= name = value`, `${name}` substitution, `$${name}` escape,
`#= prod: name = value` env overrides via `--env`. It's domain-agnostic text
machinery — no HAProxy/WireGuard knowledge in it.

```ini
# partials/backups/00-main.conf
#= tunnel      = split                 # split | full  (§4 — the only thing it changes)
#= subnet      = 10.8.2.0/24           # the pool the allocator hands /32s from
#= endpoint    = vpn.example.com:51820 # what clients dial; goes into client configs
#= dns         = 10.8.2.1
#= mtu         = 1420
#= address     = 10.8.2.1/24           # server's own address in the subnet
#= listen_port = 51820

[Interface]
Address    = ${address}
ListenPort = ${listen_port}
# No PrivateKey line: wirebard injects it into the COMPILED conf from
# server.key at build time (§8). Full-tunnel NAT PostUp lives here, hand-written.
```

The server's **public** key is derived from `server.key` at build (`wg pubkey`)
for the client render — no need to duplicate it in the partial. The **private**
key never leaves `server.key`/the 0600 compiled conf. See §8.

---

## 3. Peer partials & address allocation

**One `NN-<name>.conf` partial per peer, flat in the network folder. The set
of partials *is* the allocation ledger** — no separate state file to drift. A
peer's assigned address is its server-side `AllowedIPs` /32 (exactly what the
compiled `[Peer]` block needs), and its label rides in a leading comment
WireGuard ignores but wirebard reads:

```ini
# partials/backups/20-baki-web01.conf
# wirebard: name=baki-web01          # metadata comment; WireGuard ignores it
[Peer]
PublicKey  = <base64>                # the identity & idempotency key
AllowedIPs = 10.8.2.3/32             # THE assignment: the /32 the server routes here
```

- **Filename** = `<NN>-<sanitized-name>.conf`, `NN` = next free 10-step order
  prefix. Human-readable and merge-ordered. The filename is *not*
  authoritative — idempotency and lookups key on the `PublicKey` inside.
- The `[Peer]` block renders **verbatim** into the server conf (same as a
  haladin wish). This is why the partial IS the server-side record.

### Allocation is a pure, deterministic function

```
allocate(existing_fragments, subnet, server_address, pubkey) -> Result<Address>
```

1. If any fragment has `PublicKey == pubkey` → return its existing
   `AllowedIPs` host. **This is the idempotency guarantee** — re-adding a
   pubkey never allocates twice, never errors.
2. Else pick the **lowest-numbered free host** in `subnet`, excluding
   `server_address` and every address already in a fragment.
3. If the subnet is full → `Error{code: config}` → exit 1 (loud failure,
   never a silent reuse).

"Lowest free" makes the choice a pure function of the current fragment set —
no randomness, no clock (both are banned in the core anyway).

### The determinism caveat, stated plainly

CLAUDE.md's rule is "byte-identical output for identical inputs." Allocation
extends the definition of *input* to **include the fragment set (the
ledger)**: the address a peer gets depends on membership *history*, not just
the new pubkey (adding A then B ≠ B then A). That's inherent to stateful
membership and is fine — it's exactly why the fragments must be committed and
backed up. The compile step *given a fixed fragment set* remains
byte-identical. This caveat is the single most important thing a reviewer
should understand about the model.

**Open choice B — address reuse on removal:** when a peer is removed its /32
returns to the pool and may be handed to the next new peer. Simple and
matches "lowest free." The alternative is tombstoning (never reuse) to avoid
a recycled IP surprising downstream ACLs. I propose **reuse**; flag if your
environment pins policy to addresses.

---

## 4. `tunnel = split | full` is the *only* thing that changes between networks

Server-side, every peer is identical regardless of tunnel mode: the server
routes the peer's `/32`. The distinction lives entirely in the **client**
config wirebard renders, driven by the `#= tunnel` var in `00-main.conf`:

| `tunnel` | contract `type` | client `Address` | client `[Peer] AllowedIPs` | meaning |
|---|---|---|---|---|
| **split** | `isolated` | `10.8.2.5/24` | `10.8.2.0/24` | reach only this network's subnet |
| **full** | `proxy` | `10.8.2.5/24` | `0.0.0.0/0, ::/0` | route *all* traffic through the VPN |

(The `#=` var uses WireGuard-native words *split/full*; baki's JSON keeps its
`type: isolated|proxy` field, which maps 1:1 — see §7.)

This is the crux of baki's Option B: **only wirebard knows the policy**, so
wirebard must render the client config or the policy leaks into baki. The
footgun to get right — the client's `Address` carries the **/24** subnet
prefix (so the client installs a subnet route), while the server's
`AllowedIPs` for that peer is a **/32**. Both derive from the same assigned
host; don't conflate the prefixes.

---

## 5. Command → pipeline mapping

### `peer add --network N --pubkey K --name L --json`

```
1. Resolve project & network N.        missing project/network → exit 3 / exit 1
2. Acquire per-network lock (§6).       (RAII; released on every exit path)
3. Scan partials/N/ peer partials.
     pubkey K present?  → idempotent: reuse its address, skip to step 7.
     absent?            → allocate lowest-free /32 (§3).
4. Author the peer partial NN-<name>.conf (atomic_write).
5. build + check: merge partials → N.conf text (inject PrivateKey from
     server.key); validate invariants (address in subnet, no dup pubkey/address).
     invalid → delete the just-authored partial, exit 1.
6. apply, in this order so a crash never leaves it un-persisted:
     a. atomic_write the compiled conf to /etc/wireguard/N.conf (mode 0600)
     b. systemctl enable wg-quick@N            (idempotent; survives reboot)
     c. wg syncconf N <(new conf)              (apply live WITHOUT dropping other peers)
     apply fails → restore previous conf, delete partial, exit 3.
7. Render client_config from tunnel mode + assigned address + server
     pubkey/endpoint + DNS/MTU, with a single {{PRIVATE_KEY}} placeholder.
8. Emit exactly one JSON object to STDOUT. All logs → STDERR.
9. Release lock (RAII).
```

`wg syncconf` (not `wg-quick down/up`) is deliberate: it reconciles the live
interface to the new conf, so adding peer N never interrupts peers 1..N-1.
**Open choice C** — confirm `syncconf` is acceptable (it requires the conf on
disk and drops runtime-only state, which we have none of).

### `peer remove --network N --pubkey K --json`

Lock → find the partial by pubkey. Absent → exit 0 `{"removed": false}`.
Present → delete the partial, recompile, `wg set N peer K remove` (drop the
live session immediately) + `syncconf` + the conf is already persisted →
exit 0 `{"removed": true, …}`. Idempotent.

### `network list --json`

Read-only (shared lock or none). Enumerate `partials/*/00-main.conf`, read
each network's `#=` vars, emit the array. Never touches peers or the live
interface.

---

## 6. Concurrency — a new `FileLock` primitive

baki requires safety under concurrent invocation. We have atomic writes but
no *inter-process* lock. Add one to `fs`, RAII like `TempDir`:

```cpp
// flock(LOCK_EX) on partials/<N>/.lock, released in the destructor.
// Per-network, so provisioning different networks runs in parallel; the same
// network serializes the read→allocate→write→apply cycle.
class FileLock {
  static Result<FileLock> acquire(const std::filesystem::path& lockfile,
                                  std::chrono::seconds timeout = 30s);
  // move-only; ~FileLock releases. Bounded wait (LOCK_NB + retry) rather than
  // blocking forever — same "no unbounded hang" stance as subprocess timeouts.
};
```

---

## 7. JSON output — a pure `json` emitter + the contract, field by field

Zero deps ⇒ a small hand-rolled emitter in the pure core (escaping `"`, `\`,
control chars, newlines — `client_config` is multi-line). Exactly the kind of
pure string logic wirebard unit-tests to death. **stdout carries exactly one
JSON object; every log line goes to stderr** (contract point 1).

`peer add` envelope, matching baki's sketch verbatim:

```json
{
  "network": "backups",
  "type": "isolated",
  "address": "10.8.2.5/24",
  "server_public_key": "…",
  "endpoint": "vpn.example.com:51820",
  "client_config": "[Interface]\nPrivateKey = {{PRIVATE_KEY}}\nAddress = 10.8.2.5/24\nDNS = 10.8.2.1\nMTU = 1420\n\n[Peer]\nPublicKey = …\nEndpoint = vpn.example.com:51820\nAllowedIPs = 10.8.2.0/24\n"
}
```

Every field is derivable: `endpoint/DNS/MTU` from `00-main.conf` vars, `type`
from the `tunnel` var (split→isolated, full→proxy), `server_public_key` via
`wg pubkey < server.key`, `address` from allocation, `client_config` from §4's
policy table. All four contract guarantees hold: idempotent (§3), lone-JSON-on-
stdout, private key never received, applied+persisted-before-exit-0 (§5.6),
lock-safe (§6).

**Open choice D — errors as JSON.** baki's sketch specifies only the *success*
envelope and keys failures on exit code. I propose that under `--json`,
failures also emit a single object to stdout —
`{"error": {"code": "subnet_full", "message": "…"}}` — with the non-zero exit
still authoritative. Strictly additive; makes baki's error handling precise.
Confirm and I'll fold it into the contract reply.

Exit codes (unchanged from `commands.h`): 0 ok · 1 config/validation (bad
subnet, subnet full, dup) · 2 usage (missing `--network`/`--pubkey`) · 3
environment (no project, `wg`/`systemctl` absent, apply failed).

---

## 8. Secrets — the rules for this feature

- **Peer private key:** never received. baki sends only the pubkey. ✓
- **Server private key:** lives in `server.key` (0600, gitignored). It is
  read only when composing the interface stanza of the compiled conf (itself
  installed 0600). It must never appear on an argv (`wg`/`wg-quick` take it
  via the conf file, not the command line), in `--verbose`, or in JSON.
- **`--verbose`** may echo `wg`/`systemctl` argv (pubkeys are public) but must
  **never** print conf bodies or `client_config`.
- **`client_config`** contains only `{{PRIVATE_KEY}}` and public material
  (`server_public_key` is public by definition).

**Open choice E:** confirm the `server.key`-as-0600-file model vs. an
alternative (e.g. a keyring/agent). The file model is simplest and matches
"mirror the system."

---

## 9. Philosophy checkpoint

`client_config` means wirebard now renders a **client-side** wg-quick config —
a second artifact beyond the server `wgN.conf` it compiles. I read this as
still-one-job: it's a **computed response payload**, never persisted to the
source tree, and only wirebard holds the AllowedIPs policy that makes it
correct. So it doesn't violate "generate no artifact a human could write"
(that rule is about *checked-in* artifacts). Veto here if you disagree —
it's the one place the feature widens wirebard's remit.

---

## 10. Milestones (wirebard's M-series)

Each lands as one commit with the usual milestone storytelling; each stays
inside the layering (pure core → OS boundary → thin command).

- **M1** — port `vars` (the `#=`/`${}`/env engine, ~verbatim from haladin) +
  `project`/`resolve_project_root` (default `/etc/wireguard`) + partial loading
  (`00-main.conf` + `NN-*.conf`, NN-sorted). Pure, unit-tested.
- **M2** — `alloc`: deterministic lowest-free /32, idempotent-by-pubkey,
  exhaustion error (pure; the most heavily-tested module).
- **M3** — `compile`: merge partials → server `N.conf` (inject PrivateKey from
  server.key) + `check` invariants (golden-file tests for byte-identical output).
- **M4** — `json` emitter + the client-config render + the `peer add` /
  `network list` envelopes (pure, unit-tested).
- **M5** — `FileLock` (fs) + `apply`: install / enable / syncconf with the
  rollback of §5.6 (OS boundary).
- **M6** — CLI wiring: `peer add|remove`, `network list`, `--json`, subcommand
  dispatch, exit-code mapping, and a secrets audit pass.

---

## 11. Decisions

- **A — SETTLED (you):** default root is the WireGuard config folder
  (`/etc/wireguard`), `-C <dir>` overrides for odd locations; cwd-with-
  `partials/` for dev. Ported from haladin's `resolve_project_root`.

Still open (my recommendation is the first option in each):

- **B** — address **reuse** on removal (vs. tombstone).
- **C** — live reload via `wg syncconf` (vs. `wg-quick` strip/up).
- **D** — emit a JSON **error** object under `--json` on failure (additive to baki's sketch).
- **E** — server private key as a 0600 gitignored `server.key` file.

Sign off (or override) B–E and M1 is ready to start. Nothing here requires a
new third-party dependency — `vars`/`render`/`project` port from haladin.
```
