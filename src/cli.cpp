#include "cli.h"

#include <algorithm>
#include <array>
#include <format>

namespace wirebard {

namespace {

// Which flags take a value vs. which are on/off switches. constexpr arrays:
// baked into the binary at compile time, zero runtime setup cost.
// (-C/--dir is handled by its own branch below, before this list is consulted)
constexpr std::array kValueFlags = {"--env", "--out"};
constexpr std::array kSwitchFlags = {"--verbose", "--help"};
constexpr std::array kCommands = {"build", "check", "apply", "list"};

// std::ranges::contains (C++23): like array.includes() / slices.Contains.
bool is_value_flag(std::string_view s) { return std::ranges::contains(kValueFlags, s); }
bool is_switch(std::string_view s) { return std::ranges::contains(kSwitchFlags, s); }
bool is_command(std::string_view s) { return std::ranges::contains(kCommands, s); }

Error usage_error(std::string message) {
    // std::move: `message` is OUR copy already (passed by value on purpose —
    // callers hand us temporaries); moving it into the struct avoids copy #2.
    return Error{.code = ErrorCode::usage, .message = std::move(message)};
}

} // namespace

Result<ParsedArgs> parse_args(std::span<const char* const> argv) {
    ParsedArgs args;

    // Walk argv[1..]. subspan(1) is Go's argv[1:].
    auto rest = argv.size() > 1 ? argv.subspan(1) : std::span<const char* const>{};

    for (size_t i = 0; i < rest.size(); ++i) {
        // C++ LESSON: constructing a string_view from a C string (char*).
        // argv's memory belongs to the OS and outlives us, so viewing it is
        // safe. Comparisons on string_view are cheap and allocation-free.
        std::string_view arg = rest[i];

        if (arg == "-C" || arg == "--dir") {
            if (i + 1 >= rest.size()) {
                return std::unexpected(usage_error(std::format("{} needs a value", arg)));
            }
            args.dir = rest[++i];
        } else if (arg == "-h" || arg == "--help") {
            args.switches.emplace("--help");
        } else if (is_switch(arg)) {
            // emplace constructs the element in place inside the container.
            // (insert would also work; emplace is the reach-for-first habit.)
            args.switches.emplace(arg);
        } else if (is_value_flag(arg)) {
            if (i + 1 >= rest.size()) {
                return std::unexpected(usage_error(std::format("{} needs a value", arg)));
            }
            // map[key] = value works like JS/Go; creates or overwrites.
            args.flags[std::string(arg)] = rest[++i];
        } else if (arg.starts_with('-')) {
            return std::unexpected(usage_error(std::format("unknown flag '{}'", arg)));
        } else if (args.command.empty()) {
            if (!is_command(arg)) {
                return std::unexpected(
                    usage_error(std::format("unknown command '{}' (see --help)", arg)));
            }
            args.command = arg;
        } else {
            args.positionals.emplace_back(arg);
        }
    }

    if (args.command.empty() && !args.switches.contains("--help")) {
        return std::unexpected(usage_error("no command given (see --help)"));
    }
    return args;
}

std::string_view usage_text() {
    // A raw string literal: R"(...)" — like a JS template literal without
    // interpolation; no escaping needed inside. Static storage, lives forever,
    // so returning a view of it is safe (THIS is when returning string_view
    // is okay: the viewed data outlives every possible caller).
    return R"(wirebard — WireGuard config manager & compiler

Usage: wirebard [-C DIR] <command> [network] [options]

Commands:
  check    Validate a network's sources without touching anything
  build    Compile the sources into a wireguard config          (stub)
  apply    Install the config and restart the interface          (stub)
  list     Show what's defined where                             (stub)

With no [network], check runs over every network under partials/.

Global:
  -C, --dir DIR   project directory (default: current dir, else /etc/wireguard)
  --env NAME      select a '#= NAME: ...' variable overlay
  -h, --help      this text
  --verbose       echo external commands as they run

Exit codes: 0 ok · 1 config/validation failure · 2 usage · 3 environment
)";
}

} // namespace wirebard
