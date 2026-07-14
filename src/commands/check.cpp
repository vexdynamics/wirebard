// commands/check.cpp — `wirebard check [network]`: validate the sources without
// touching anything. Thin shell over check_network(); all the logic is there.
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "commands.h"
#include "project.h"

namespace wirebard {

namespace {

std::optional<std::string_view> env_flag(const ParsedArgs& args) {
    auto it = args.flags.find("--env");
    if (it == args.flags.end())
        return std::nullopt;
    return it->second;
}

} // namespace

int cmd_check(const ParsedArgs& args) {
    auto root = resolve_project_root(args.dir);
    if (!root) {
        std::println(stderr, "error: {}", format_error(root.error()));
        return kExitEnvironment;
    }
    auto paths = ProjectPaths::locate(*root);
    if (!paths) {
        std::println(stderr, "error: {}", format_error(paths.error()));
        return kExitEnvironment;
    }

    // A positional names one network; otherwise check every network.
    std::vector<std::string> names;
    if (!args.positionals.empty()) {
        names.push_back(args.positionals.front());
    } else {
        auto nets = list_networks(*paths);
        if (!nets) {
            std::println(stderr, "error: {}", format_error(nets.error()));
            return kExitEnvironment;
        }
        for (const auto& n : *nets)
            names.push_back(n.name);
    }

    if (names.empty()) {
        std::println("no networks found under {}", paths->partials_dir.string());
        return kExitOk;
    }

    int worst = kExitOk;
    for (const std::string& name : names) {
        auto net = Network::load(*paths, name, env_flag(args));
        if (!net) {
            std::println(stderr, "{}: {}", name, format_error(net.error()));
            worst = kExitFailure;
            continue;
        }

        auto diags = check_network(*net);
        if (diags.empty()) {
            std::println("{}: ok ({} partials)", name, net->partials.size());
            continue;
        }
        for (const Diagnostic& d : diags) {
            const std::string_view sev = d.severity == Severity::error ? "error" : "warning";
            std::println(stderr, "{}: {}: {}", name, sev, format_error(d.error));
            if (d.severity == Severity::error)
                worst = kExitFailure;
        }
    }
    return worst;
}

} // namespace wirebard
