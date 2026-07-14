// commands/build.cpp — `wirebard build [network]`: compile sources into
// /etc/wireguard/<network>.conf (0600), validating first. Does NOT touch the
// running interface — that's `apply`.
#include <filesystem>
#include <print>

#include "apply.h" // compile_network
#include "check.h"
#include "commands.h"
#include "commands/common.h"
#include "fs.h"
#include "project.h"

namespace wirebard {

int cmd_build(const ParsedArgs& args) {
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
        // 0600: the compiled conf carries the server PrivateKey.
        auto w = atomic_write_file(net->paths.output, *conf,
                                   std::filesystem::perms::owner_read |
                                       std::filesystem::perms::owner_write);
        if (!w) {
            std::println(stderr, "{}: {}", name, format_error(w.error()));
            worst = kExitEnvironment;
            continue;
        }
        std::println("{}: wrote {} ({} bytes, 0600)", name, net->paths.output.string(),
                     conf->size());
    }
    return worst;
}

} // namespace wirebard
