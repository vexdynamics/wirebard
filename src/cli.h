// cli.h — hand-rolled argv parsing.
//
// In production C++ you'd likely reach for CLI11 (the commander/cobra of
// C++). Our surface is a handful of subcommands and flags, so we hand-roll
// it — partly because a dependency should pay rent, partly because this is a
// nice tour of string handling without allocation.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "error.h"

namespace wirebard {

// Everything main() needs to dispatch a command. Individual commands validate
// their own flags out of the generic bags below (keeps this parser dumb).
struct ParsedArgs {
    // -C / --dir. nullopt = not given: commands will auto-resolve the project
    // once the layout is designed (cwd first, then the system location).
    std::optional<std::filesystem::path> dir;
    std::string command; // "build", "check", ...
    std::vector<std::string> positionals;

    // C++ LESSON: std::map is an ORDERED map (red-black tree) — Go's map +
    // sorted iteration in one. The odd-looking `std::less<>` comparator
    // enables "heterogeneous lookup": flags.find(string_view) without first
    // constructing a std::string. Without it, every lookup with a literal
    // would allocate a temporary string just to compare. Small thing,
    // idiomatic thing.
    std::map<std::string, std::string, std::less<>> flags; // --env prod, --out path
    std::set<std::string, std::less<>> switches;           // --verbose, --help
};

// C++ LESSON: std::span<T> = pointer + length over someone else's contiguous
// array — Go's []T slice, exactly. Here it wraps main()'s raw argv without
// copying. Like string_view, it borrows: don't keep it past the call.
Result<ParsedArgs> parse_args(std::span<const char* const> argv);

std::string_view usage_text();

} // namespace wirebard
