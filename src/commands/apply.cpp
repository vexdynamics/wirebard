// commands/apply.cpp — `wirebard apply [network] [--dry-run]`: build, validate,
// then install + reload the live interface. Holds the per-network lock while it
// mutates. --dry-run prints the plan and touches nothing.
#include <print>

#include "apply.h"
#include "check.h"
#include "commands.h"
#include "commands/common.h"
#include "fs.h"
#include "project.h"

namespace wirebard {

int cmd_apply(const ParsedArgs& args) {
    const bool dry = args.switches.contains("--dry-run");

    auto paths = cmd::project_of(args);
    if (!paths) {
        std::println(stderr, "error: {}", format_error(paths.error()));
        return kExitEnvironment;
    }
    auto names = cmd::select_networks(args, *paths);
    if (!names) {
        std::println(stderr, "error: {}", format_error(names.error()));
        return kExitEnvironment;
    }
    if (names->empty()) {
        std::println("no networks found under {}", paths->partials_dir.string());
        return kExitOk;
    }

    int worst = kExitOk;
    for (const std::string& name : *names) {
        auto net = Network::load(*paths, name, cmd::env_flag(args));
        if (!net) {
            std::println(stderr, "{}: {}", name, format_error(net.error()));
            worst = kExitFailure;
            continue;
        }

        bool invalid = false;
        for (const Diagnostic& d : check_network(*net)) {
            if (d.severity == Severity::error) {
                std::println(stderr, "{}: error: {}", name, format_error(d.error));
                invalid = true;
            }
        }
        if (invalid) {
            worst = kExitFailure;
            continue;
        }

        auto conf = compile_network(*net);
        if (!conf) {
            std::println(stderr, "{}: {}", name, format_error(conf.error()));
            worst = kExitEnvironment;
            continue;
        }

        // Serialize with any concurrent peer add/remove on this network.
        auto lock = FileLock::acquire(net->paths.dir / ".lock");
        if (!lock) {
            std::println(stderr, "{}: {}", name, format_error(lock.error()));
            worst = kExitEnvironment;
            continue;
        }

        if (dry)
            std::println("{}: would apply —", name);
        auto r = execute_apply(plan_apply(net->paths, *conf), dry);
        if (!r) {
            std::println(stderr, "{}: {}", name, format_error(r.error()));
            worst = kExitEnvironment;
            continue;
        }
        if (!dry)
            std::println("{}: applied", name);
    }
    return worst;
}

} // namespace wirebard
