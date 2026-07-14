// main.cpp — parse argv, dispatch to a command, map errors to exit codes.
// Kept as thin as a Go main(): all logic lives in the library (wirebard_lib),
// which is what the tests link against. main.cpp itself can't be unit-tested.
#include <print>
#include <span>

#include "cli.h"
#include "commands.h"
#include "error.h"
#include "subprocess.h"

int main(int argc, char** argv) {
    // Wrap C's argc/argv in a span immediately — from here on, no pointer
    // arithmetic, and .size() instead of carrying argc around.
    auto args = wirebard::parse_args(std::span(argv, static_cast<size_t>(argc)));
    if (!args) {
        // std::println to stderr. In Go: fmt.Fprintln(os.Stderr, ...).
        std::println(stderr, "error: {}", wirebard::format_error(args.error()));
        std::println(stderr, "run 'wirebard --help' for usage");
        return wirebard::kExitUsage;
    }

    if (args->switches.contains("--help")) {
        std::print("{}", wirebard::usage_text());
        return wirebard::kExitOk;
    }

    wirebard::set_verbose(args->switches.contains("--verbose"));

    // C++ LESSON: there's no switch on strings in C++ (switch works on
    // integers only). An if-chain over string_view is the plain, correct
    // answer at this size.
    const std::string& cmd = args->command;
    if (cmd == "build")
        return wirebard::cmd_build(*args);
    if (cmd == "check")
        return wirebard::cmd_check(*args);
    if (cmd == "apply")
        return wirebard::cmd_apply(*args);
    if (cmd == "list")
        return wirebard::cmd_list(*args);
    if (cmd == "peer")
        return wirebard::cmd_peer(*args);
    if (cmd == "network")
        return wirebard::cmd_network(*args);

    // parse_args validated the command, so this is unreachable — but the
    // compiler can't prove that, and main must return something.
    std::println(stderr, "error: unhandled command '{}'", cmd);
    return wirebard::kExitUsage;
}
