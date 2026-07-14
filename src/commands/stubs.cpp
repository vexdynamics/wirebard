// Every command, stubbed. Each one graduates into its own file (with real
// logic in the library modules, not here) as the milestones progress.
#include <print>

#include "commands.h"

namespace wirebard {

namespace {

int not_implemented(std::string_view name) {
    std::println(stderr, "'{}' is not implemented yet — wirebard is a fresh scaffold", name);
    return kExitUsage;
}

} // namespace

// cmd_check has graduated into commands/check.cpp (M3).
int cmd_build(const ParsedArgs&) { return not_implemented("build"); }
int cmd_apply(const ParsedArgs&) { return not_implemented("apply"); }
int cmd_list(const ParsedArgs&) { return not_implemented("list"); }

} // namespace wirebard
