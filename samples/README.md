# samples/ — the partial layout wirebard compiles

These are reference partials showing how config fragments compose into
`/etc/wireguard/<network>.conf`. Nothing here is compiled by wirebard — it's
documentation you can copy into a real project root.

## Layout

A project root mirrors `/etc/wireguard/` (wirebard's default location; override
with `-C <dir>` when it lives somewhere strange):

```
/etc/wireguard/                      ← project root (default; -C to override)
├── backups.conf                     ← COMPILED output; wg-quick@backups reads this
├── roam.conf                        ← COMPILED output; wg-quick@roam reads this
└── partials/
    ├── backups/                     ← one folder per network (= per interface)
    │   ├── server.key               ← 0600, gitignored, NEVER emitted (§ secrets)
    │   ├── template.conf            ← copy-me; NOT compiled
    │   ├── 00-main.conf             ← [Interface] + all #= variables
    │   ├── 10-alice.conf            ← one [Peer] per file, merged in NN- order
    │   └── 20-web01.conf
    └── roam/
        ├── 00-main.conf             ← tunnel = full (road-warrior)
        └── 10-laptop.conf
```

`--network backups` ⇒ folder `partials/backups/` ⇒ compiled `backups.conf` ⇒
systemd unit `wg-quick@backups`. The folder name is the network name is the
interface name.

## The two renders

wirebard produces two *different* things from one set of partials:

**1. The server config** (`backups.conf`) — the partials merged verbatim in
`NN-` order, `${vars}` substituted, and `PrivateKey` injected from
`server.key`. `00-main.conf` + `10-alice.conf` + `20-web01.conf` →

```ini
[Interface]                       # from 00-main.conf, ${vars} resolved
Address    = 10.8.2.1/24
ListenPort = 51820
PrivateKey = <injected from server.key, 0600>

[Peer]                            # from 10-alice.conf, verbatim
PublicKey  = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=
AllowedIPs = 10.8.2.2/32

[Peer]                            # from 20-web01.conf, verbatim
PublicKey  = HIgo9xNzJMWLKASShiTqIybxZ0U3wGLiUeJ1PKf8ykw=
AllowedIPs = 10.8.2.3/32
```

**2. The client config** — computed per peer, returned by `peer add --json`,
never written to the tree. This is where `tunnel = split|full` matters (the
server side above is identical either way):

```ini
# tunnel = split (backups)          # tunnel = full (roam)
[Interface]                         [Interface]
PrivateKey = {{PRIVATE_KEY}}        PrivateKey = {{PRIVATE_KEY}}
Address    = 10.8.2.3/24            Address    = 10.9.0.2/24
DNS        = 10.8.2.1               DNS        = 1.1.1.1

[Peer]                              [Peer]
PublicKey  = <server pub>           PublicKey  = <server pub>
Endpoint   = vpn.example.com:51820  Endpoint   = vpn.example.com:51821
AllowedIPs = 10.8.2.0/24            AllowedIPs = 0.0.0.0/0, ::/0
```

`{{PRIVATE_KEY}}` is the only placeholder: the caller generated the keypair
locally and substitutes its own private key. wirebard only ever sees the public
key.

## Variables (`#=`)

Declared once in `00-main.conf`, used anywhere as `${name}`. Per-environment
overrides work the same way (`#= prod: endpoint = …`, selected with `--env
prod`). See `partials/backups/00-main.conf` for the full set.
