# wirebard

WireGuard config manager & compiler — the sibling of
[haladin](../haladin), seeded from its scaffold and built on the same
philosophy: do exactly one job, own no orchestration, generate no artifact
a human could write.

**Status: M1–M6 landed.** The domain model (partials + variables, address
allocation, compile/check, client-config + JSON contract, apply, and the
`peer`/`network` commands baki drives over SSH) is implemented and tested. The
one unverified surface is the live `wg`/`systemctl` execution in `apply`, which
needs a real WireGuard host — `--dry-run` shows exactly what it would run. See
`docs/design/peer-provisioning.md` for the design and the baki CLI contract,
and `samples/` for the partial layout.

Also: a C++23 learning project. The source carries `C++ LESSON:` teaching
comments aimed at someone coming from Node.js/Go.

## Requirements

gcc 14+ (C++23), cmake 3.28+, ninja. No third-party libraries.

## Build

```bash
cmake --preset debug            # configure (once; ASan+UBSan on)
cmake --build --preset debug    # compile
ctest --preset debug            # run the test suite
./build/debug/src/wirebard --help
```

`cmake --preset release` / `--build --preset release` for the optimized
binary. No toolchain on the host? `docker build -t wirebard .` compiles and
runs the suite in a clean room (`docker compose run --rm ci` for just the
tests).

## Releases

Pushing a `vX.Y.Z` tag triggers `.github/workflows/release.yml`: it builds
**fully static** binaries for x86_64, aarch64, armhf, and armel (servers and
Raspberry Pis old and new — no glibc to match), runs the whole test suite
against each (ARM under qemu), and publishes a GitHub Release with tarballs and
`SHA256SUMS`. Reproduce the x86_64 artifact locally:

```bash
cmake --preset release-static -DWIREBARD_VERSION=vX.Y.Z
cmake --build --preset release-static
./build/release-static/src/wirebard --version   # -> wirebard vX.Y.Z
```

Every push/PR runs `.github/workflows/ci.yml`: the suite under ASan+UBSan plus
the clean-room `docker build`. `wirebard --version` prints `dev` for untagged
local builds.

## Development notes

- Debug preset compiles with AddressSanitizer + UBSan — keep it on.
- `clang-format -i src/*.cpp src/*.h src/commands/*.cpp tests/*.cpp` before
  committing; `clang-tidy -p build/debug src/*.cpp` for the linter.
- House rules live in CLAUDE.md; the architecture conventions (errors as
  values, pure core vs OS boundary vs thin commands, determinism, loud
  failure) are inherited from haladin and non-negotiable.
