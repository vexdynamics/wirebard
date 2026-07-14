// subprocess.h — run external commands, capture output, enforce timeouts.
//
// Node gives you child_process.execFile, Go gives you os/exec — batteries
// included. C++ gives you the raw POSIX syscalls (fork, execvp, pipe, poll,
// waitpid), so this module IS the batteries. Deliberate choices:
//
//   * No shell. popen()/system() run through /bin/sh, which turns every
//     path with a space — or a hostile filename — into a shell-injection
//     bug. We exec the binary directly with an argv array, like execFile
//     (not exec) in Node. There is nothing to quote, ever.
//   * Explicit timeout. Unix gives subprocesses NO default timeout; a hung
//     `wg`/`systemctl` would hang wirebard forever. poll() + SIGKILL
//     enforces one.
//   * stdout and stderr captured separately — tools like wg(8) write their
//     verdict to stderr, and mixing streams loses information.
#pragma once

#include <chrono>
#include <span>
#include <string>

#include "error.h"

namespace wirebard {

struct CommandResult {
    int exit_code = -1; // -1 if the process died from a signal
    std::string out;
    std::string err;

    [[nodiscard]] bool ok() const noexcept { return exit_code == 0; }
};

// argv[0] is the program (resolved via PATH). The Error path is for FAILURE
// TO RUN (fork/pipe/exec problems, timeout); a program that runs and exits
// non-zero is a SUCCESSFUL call with exit_code != 0 — the distinction between
// "couldn't ask the question" and "the answer was no".
Result<CommandResult> run_command(std::span<const std::string> argv,
                                  std::chrono::seconds timeout = std::chrono::seconds{120});

// --verbose support: when on, every run_command echoes its argv to stderr
// ("+ wg show ..."), so any wg/systemctl invocation can be
// copy-pasted and rerun by hand. Set once from main; read at each spawn.
void set_verbose(bool on) noexcept;

} // namespace wirebard
