// commands/common.h — tiny inline helpers shared by the command shells:
// reading flags, resolving the project, selecting which networks to act on.
// Header-only (inline) so several command TUs can share them without a .cpp.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli.h"
#include "error.h"
#include "project.h"

namespace wirebard::cmd {

// A value flag's value, or nullopt if not given.
inline std::optional<std::string_view> flag(const ParsedArgs& args, std::string_view name) {
    auto it = args.flags.find(name);
    return it == args.flags.end() ? std::nullopt : std::optional<std::string_view>(it->second);
}

inline std::optional<std::string_view> env_flag(const ParsedArgs& args) {
    return flag(args, "--env");
}

// Resolve -C / cwd / /etc/wireguard, then validate it's a project.
inline Result<ProjectPaths> project_of(const ParsedArgs& args) {
    auto root = resolve_project_root(args.dir);
    if (!root)
        return std::unexpected(root.error());
    return ProjectPaths::locate(*root);
}

// The networks a whole-project command acts on: the positional names one,
// otherwise every network under partials/.
inline Result<std::vector<std::string>> select_networks(const ParsedArgs& args,
                                                        const ProjectPaths& paths) {
    std::vector<std::string> names;
    if (!args.positionals.empty()) {
        names.push_back(args.positionals.front());
        return names;
    }
    auto nets = list_networks(paths);
    if (!nets)
        return std::unexpected(nets.error());
    for (const auto& n : *nets)
        names.push_back(n.name);
    return names;
}

} // namespace wirebard::cmd
