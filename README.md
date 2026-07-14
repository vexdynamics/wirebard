# wirebard

WireGuard config manager & compiler — the sibling of
[haladin](../haladin), seeded from its scaffold and built on the same
philosophy: do exactly one job, own no orchestration, generate no artifact
a human could write.

**Status: fresh scaffold.** The foundation (error model, atomic file I/O,
subprocess runner, CLI parser, tooling) is proven code inherited from
haladin; every command is a stub until the WireGuard domain model is
designed.

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

## Development notes

- Debug preset compiles with AddressSanitizer + UBSan — keep it on.
- `clang-format -i src/*.cpp src/*.h src/commands/*.cpp tests/*.cpp` before
  committing; `clang-tidy -p build/debug src/*.cpp` for the linter.
- House rules live in CLAUDE.md; the architecture conventions (errors as
  values, pure core vs OS boundary vs thin commands, determinism, loud
  failure) are inherited from haladin and non-negotiable.
