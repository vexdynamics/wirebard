// commands/list.cpp — `wirebard list`: a human overview of every network and
// its peers. (Machine callers want `network list --json` instead.)
#include <print>

#include "commands.h"
#include "commands/common.h"
#include "net.h"
#include "peer.h"
#include "project.h"

namespace wirebard {

int cmd_list(const ParsedArgs& args) {
    auto paths = cmd::project_of(args);
    if (!paths) {
        std::println(stderr, "error: {}", format_error(paths.error()));
        return kExitEnvironment;
    }
    auto nets = list_networks(*paths);
    if (!nets) {
        std::println(stderr, "error: {}", format_error(nets.error()));
        return kExitEnvironment;
    }
    if (nets->empty()) {
        std::println("no networks found under {}", paths->partials_dir.string());
        return kExitOk;
    }

    for (const NetworkPaths& np : *nets) {
        auto net = Network::load(*paths, np.name, cmd::env_flag(args));
        if (!net) {
            std::println(stderr, "{}: {}", np.name, format_error(net.error()));
            continue;
        }
        const auto tunnel = net->vars.get("tunnel").value_or("split");
        const auto subnet = net->vars.get("subnet").value_or("?");
        auto assigns = collect_assignments(net->partials);
        const size_t count = assigns ? assigns->size() : 0;

        std::println("{}  [{} tunnel, {}]  {} peer(s)", np.name, tunnel, subnet, count);
        if (assigns) {
            for (const Assignment& a : *assigns) {
                std::println("    {:<24} {}", a.name.empty() ? a.public_key : a.name,
                             format_ipv4(a.address));
            }
        }
    }
    return kExitOk;
}

} // namespace wirebard
