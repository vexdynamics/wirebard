// commands.h — the subcommands, each a free function taking parsed args and
// returning a process exit code.
//
// Design note (inherited from haladin, this scaffold's parent): commands are
// deliberately thin orchestrators over library modules; everything worth
// testing lives in a pure core the commands merely call. All four are stubs
// until wirebard's domain model (interfaces? peers? how do fragments
// compose into wg0.conf?) is designed.
#pragma once

#include "cli.h"

namespace wirebard {

// Exit codes, as agreed: 0 ok, 1 config/validation failure, 2 usage,
// 3 environment problems (missing tool, invalid project dir).
inline constexpr int kExitOk = 0;
inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitEnvironment = 3;
// C++ LESSON: `inline constexpr` in a header = one shared compile-time
// constant, no ODR trouble. The old-school equivalent was a #define; never
// use #define for constants in C++ (no type, no scope, invisible to tooling).

int cmd_build(const ParsedArgs& args);
int cmd_check(const ParsedArgs& args);
int cmd_apply(const ParsedArgs& args);
int cmd_list(const ParsedArgs& args);

// Command families: the subcommand is positionals[0] (add/remove, list).
int cmd_peer(const ParsedArgs& args);    // peer add|remove — the baki contract
int cmd_network(const ParsedArgs& args); // network list

} // namespace wirebard
