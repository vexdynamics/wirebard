# Peer provisioning — domain model & the machine-caller CLI contract

**Status:** this is the design of record for the implemented feature. It turned
wirebard's four stub commands into a real tool, driven by its first concrete
consumer: a machine caller that invokes wirebard over SSH to add/remove
WireGuard peers.

It settles the questions the project left open — the source format, how
fragments compose into `/etc/wireguard/<network>.conf`, and what each command
really does — by designing them against that caller's needs. Section 7 gives the
JSON contract field by field.

---

## 0. The one decision that shapes everything

The caller wants an **imperative, mutating** API: `wirebard peer add … --json`
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
is the same deterministic compile any hand-edit would trigger. This is why a
"drop a fragment over SSH" interface was rejected as the *interface* while
remaining wirebard's *internal model*: the caller gets a clean imperative call,
wirebard stays a compiler underneath.

Everything below follows from holding that line.

---

## 1. Filesystem layout — the partials model

The project directory is a set of *partials*. WireGuard is **one file per
interface**, so each network is a partials subfolder that compiles to its own
`<network>.conf`. See `samples/` for working examples.

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
    │   └── 20-web01.conf
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
override for odd locations. Explicit `-C <dir>` wins; else the current directory
if it contains `partials/` (dev workflow); else `/etc/wireguard`.

---

## 2. `00-main.conf` — the interface stanza + variables (hand-written)

Sorts first in its folder, so it owns the `[Interface]` stanza *and* is the
single place every `#=` variable is declared. The **`vars` module is
domain-agnostic text machinery** — no HAProxy/WireGuard knowledge in it:
`#= name = value`, `${name}` substitution, `$${name}` escape, `#= prod: name =
value` env overrides via `--env`.

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

The server's **public** key rides in a `#= server_public_key` var (it's public,
so a partial is a fine home) and is emitted into every client render. The
**private** key never leaves `server.key`/the 0600 compiled conf. See §8.

> Implemented deviation: an earlier draft derived the public key from
> `server.key` via `wg pubkey` at build time. `wg pubkey` reads the private key
> on **stdin**, and wirebard's `subprocess` runner has no stdin piping (and
> won't grow a shell to get `<()`), so the public key is a declared var instead.
> Generate the pair once: `wg genkey | tee server.key | wg pubkey`.

---

## 3. Peer partials & address allocation

**One `NN-<name>.conf` partial per peer, flat in the network folder. The set
of partials *is* the allocation ledger** — no separate state file to drift. A
peer's assigned address is its server-side `AllowedIPs` /32 (exactly what the
compiled `[Peer]` block needs), and its label rides in a leading comment
WireGuard ignores but wirebard reads:

```ini
# partials/backups/20-web01.conf
# wirebard: name=web01               # metadata comment; WireGuard ignores it
[Peer]
PublicKey  = <base64>                # the identity & idempotency key
AllowedIPs = 10.8.2.3/32             # THE assignment: the /32 the server routes here
```

- **Filename** = `<NN>-<sanitized-name>.conf`, `NN` = next free 10-step order
  prefix. Human-readable and merge-ordered. The filename is *not*
  authoritative — idempotency and lookups key on the `PublicKey` inside.
- The `[Peer]` block renders **verbatim** into the server conf. This is why the
  partial IS the server-side record.

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

The project's rule is "byte-identical output for identical inputs." Allocation
extends the definition of *input* to **include the fragment set (the
ledger)**: the address a peer gets depends on membership *history*, not just
the new pubkey (adding A then B ≠ B then A). That's inherent to stateful
membership and is fine — it's exactly why the fragments must be committed and
backed up. The compile step *given a fixed fragment set* remains
byte-identical. This caveat is the single most important thing a reviewer
should understand about the model.

**Address reuse on removal (settled):** when a peer is removed its /32 returns
to the pool and may be handed to the next new peer. Simple and matches "lowest
free." The alternative is tombstoning (never reuse) to avoid a recycled IP
surprising downstream ACLs — reuse was chosen.

---

## 4. `tunnel = split | full` is the *only* thing that changes between networks

Server-side, every peer is identical regardless of tunnel mode: the server
routes the peer's `/32`. The distinction lives entirely in the **client**
config wirebard renders, driven by the `#= tunnel` var in `00-main.conf`:

| `tunnel` | contract `type` | client `Address` | client `[Peer] AllowedIPs` | meaning |
|---|---|---|---|---|
| **split** | `isolated` | `10.8.2.5/24` | `10.8.2.0/24` | reach only this network's subnet |
| **full** | `proxy` | `10.8.2.5/24` | `0.0.0.0/0, ::/0` | route *all* traffic through the VPN |

(The `#=` var uses WireGuard-native words *split/full*; the JSON contract keeps
its `type: isolated|proxy` field, which maps 1:1 — see §7.)

This is the crux of Option B: **only wirebard knows the policy**, so wirebard
must render the client config or the policy leaks into the caller. The footgun
to get right — the client's `Address` carries the **/24** subnet prefix (so the
client installs a subnet route), while the server's `AllowedIPs` for that peer
is a **/32**. Both derive from the same assigned host; don't conflate the
prefixes.

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
interface to the new conf, so adding peer N never interrupts peers 1..N-1. It
requires the conf on disk and drops runtime-only state, of which we have none.

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

## 6. Concurrency — a `FileLock` primitive

The contract requires safety under concurrent invocation. We have atomic writes
but no *inter-process* lock, so one lives in `fs`, RAII like `TempDir`:

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

`peer add` envelope:

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

Every field is derivable: `endpoint/DNS/MTU/server_public_key` from
`00-main.conf` vars, `type` from the `tunnel` var (split→isolated, full→proxy),
`address` from allocation, `client_config` from §4's policy table. All four
contract guarantees hold: idempotent (§3), lone-JSON-on-stdout, private key
never received, applied+persisted-before-exit-0 (§5.6), lock-safe (§6).

**Errors as JSON (open):** the success envelope is defined above; failures key
on exit code. A future addition, under `--json`, could also emit a single
object to stdout — `{"error": {"code": "subnet_full", "message": "…"}}` — with
the non-zero exit still authoritative. Strictly additive.

Exit codes (from `commands.h`): 0 ok · 1 config/validation (bad subnet, subnet
full, dup) · 2 usage (missing `--network`/`--pubkey`, or a `--pubkey` that isn't
a base64 WireGuard key — 44 chars) · 3 environment (no project, `wg`/`systemctl`
absent, apply failed).

`peer add`/`peer remove` reject an empty or non-base64 `--pubkey` up front
(exit 2), before any allocation or filesystem work — the contract does not
trust callers to send a well-formed key.

---

## 8. Secrets — the rules for this feature

- **Peer private key:** never received. The caller sends only the pubkey. ✓
- **Server private key:** lives in `server.key` (0600, gitignored). It is
  read only when composing the interface stanza of the compiled conf (itself
  installed 0600). It must never appear on an argv (`wg`/`wg-quick` take it
  via the conf file, not the command line), in `--verbose`, or in JSON.
- **`--verbose`** may echo `wg`/`systemctl` argv (pubkeys are public) but must
  **never** print conf bodies or `client_config`.
- **`client_config`** contains only `{{PRIVATE_KEY}}` and public material
  (`server_public_key` is public by definition).

The `server.key`-as-0600-file model is used (vs. a keyring/agent): simplest, and
it matches "mirror the system."

---

## 9. Philosophy checkpoint

`client_config` means wirebard renders a **client-side** wg-quick config — a
second artifact beyond the server `<network>.conf` it compiles. This is
still-one-job: it's a **computed response payload**, never persisted to the
source tree, and only wirebard holds the AllowedIPs policy that makes it
correct. So it doesn't violate "generate no artifact a human could write"
(that rule is about *checked-in* artifacts) — it's the one place the feature
widens wirebard's remit.

---

## 10. Milestones (wirebard's M-series) — all landed

Each landed as one commit with the usual milestone storytelling; each stays
inside the layering (pure core → OS boundary → thin command). **M1–M6 are
implemented and tested** (`ctest --preset debug`); the only unverified surface
is the live `wg`/`systemctl` execution in `execute_apply`, which needs a real
WireGuard host — `--dry-run` shows exactly what it runs.

- **M1** — `vars` (the `#=`/`${}`/env engine) + `project`/`resolve_project_root`
  (default `/etc/wireguard`) + partial loading (`00-main.conf` + `NN-*.conf`,
  NN-sorted). Pure, unit-tested.
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

Settled during design and implementation:

- **A** — default root is the WireGuard config folder (`/etc/wireguard`),
  `-C <dir>` overrides for odd locations; cwd-with-`partials/` for dev.
- **B** — address **reuse** on removal (vs. tombstone).
- **C** — live reload via `wg syncconf` (vs. `wg-quick` strip/up).
- **D** — JSON **error** objects under `--json` on failure remain an optional
  future addition; failures currently key on exit code.
- **E** — server private key as a 0600 gitignored `server.key` file.

Nothing here required a new third-party dependency — `vars`/`render`/`project`
are pure, dependency-free modules.
