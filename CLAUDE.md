# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

wirebard is a WireGuard config manager/compiler CLI (C++23), the sibling of
haladin (../haladin — the HAProxy config manager this scaffold was seeded
from). The domain model is NOT designed yet: how config fragments compose
into `/etc/wireguard/wgN.conf`, what the source format looks like, and what
each command really does are open questions. What IS settled is the
philosophy, inherited verbatim from haladin: the tool does exactly its one
job and nothing else — bloat is a bug, orchestration belongs to standard
tools, artifacts a human could own (compose files, Dockerfiles, templates)
are hand-written and never generated, and the project directory should
mirror the real system location so backing it up recreates the server.
Zero third-party dependencies.

**This is also a C++ learning project.** The owner (Kristian) comes from
Node.js/Go and is learning C++ through this codebase (continuing from
haladin). All code carries `C++ LESSON:` teaching comments that map concepts
to Node/Go equivalents. When writing new code here: keep that style — teach
each new C++ concept once, at its first use, with a Node/Go analogy; don't
narrate every line. Concepts already taught in the seeded modules (RAII,
Result/expected, string_view lifetime, sink arguments...) don't need
re-teaching — build on them. Idioms must stay veteran-grade modern C++23.
Keep the milestone storytelling in commit messages (haladin reached M8;
wirebard starts its own M-series at M1).

## Commands

```bash
cmake --preset debug              # configure; only needed after CMakeLists changes
cmake --build --preset debug      # build (ASan + UBSan active in debug)
ctest --preset debug              # all tests
./build/debug/tests/wirebard_tests --gtest_filter='Cli.*'  # finer filter
cmake --preset release && cmake --build --preset release   # optimized build

clang-format -i src/*.cpp src/*.h src/commands/*.cpp tests/*.cpp
clang-tidy -p build/debug src/*.cpp src/commands/*.cpp     # expect 0 warnings
```

The test suite has no external dependencies.

## Architecture

Static library `wirebard_lib` (all logic, everything under `src/`) + thin
`main.cpp`; tests link the library. Seeded foundation:

- `error` — the house error model: `Result<T> = std::expected<T, Error>`,
  `SourceLoc` pointing at the USER's file:line, `Diagnostic` for
  collect-everything analysis passes. Add domain `ErrorCode`s as needed.
- `fs` — whole-file read, ATOMIC write (temp+fsync+rename — mandatory for
  configs a live daemon reads), sorted suffix-filtered `list_files`, RAII
  TempDir.
- `subprocess` — fork/execvp + pipes + poll timeout, never a shell; this is
  how `wg`, `wg-quick`, and `systemctl` will be invoked. `--verbose` echoes
  every argv, copy-pasteable.
- `cli` — dumb argv parser; commands validate their own flags. All four
  commands (build/check/apply/list) are stubs in `commands/stubs.cpp`.

Cross-cutting conventions (violating these is a bug, not a style choice):

- **Errors are values**: every fallible function returns `Result<T>`. No
  exceptions anywhere.
- **Layering**: pure core (no I/O, fully unit-tested) / OS boundary
  (fs, subprocess, tool wrappers) / thin command shells with no logic worth
  testing directly.
- **Determinism**: compilation of user configs must be byte-identical for
  identical inputs.
- **Loud failure over silent surprise**: a typo (unknown env, unknown name)
  is an error, never a silently-different build.
- **Exit codes**: 0 ok, 1 config/validation failure, 2 usage,
  3 environment. Defined in commands.h.
- Secrets caution (WireGuard-specific): private keys will pass through this
  tool. They must never land in logs, `--verbose` output, error messages,
  or world-readable files — decide the handling model before writing the
  first line that touches one.

## Gotchas

- `.clang-tidy` disables specific checks with documented reasons in the file
  — read them before re-enabling or adding suppressions. clang-format owns
  wrapping/brace style; don't fight it by hand.
- `= {}` on optional aggregate members has a NOLINT (error.h): gcc's
  -Wmissing-field-initializers and clang-tidy's redundant-member-init
  disagree; gcc wins.
- Debug preset runs ASan+UBSan — keep it on; lifetime bugs (string_view
  dangling, fd double-close) must crash in tests, not ship.
- clangd reads `compile_commands.json` symlinked at the repo root from
  `build/debug/` (regenerate by configuring the debug preset).
