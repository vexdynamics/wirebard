// commands/network.cpp — `wirebard network list [--json]`: the machine-readable
// network inventory from the baki contract (docs §7).
#include <print>
#include <string>
#include <vector>

#include "commands.h"
#include "commands/common.h"
#include "contract.h"
#include "project.h"

namespace wirebard {

int cmd_network(const ParsedArgs& args) {
    const std::string sub = args.positionals.empty() ? "" : args.positionals.front();
    if (sub != "list") {
        std::println(stderr, "usage: wirebard network list [--json]");
        return kExitUsage;
    }
    const bool json = args.switches.contains("--json");

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

    std::vector<NetworkSummary> summaries;
    for (const NetworkPaths& np : *nets) {
        auto net = Network::load(*paths, np.name, cmd::env_flag(args));
        if (!net) {
            std::println(stderr, "error: {}", format_error(net.error()));
            return kExitFailure;
        }
        summaries.push_back(
            NetworkSummary{.name = np.name,
                           .full_tunnel = net->vars.get("tunnel").value_or("split") == "full",
                           .subnet = std::string(net->vars.get("subnet").value_or(""))});
    }

    if (json) {
        std::println("{}", network_list_json(summaries));
    } else {
        for (const NetworkSummary& s : summaries)
            std::println("{}  {}  {}", s.name, s.full_tunnel ? "proxy" : "isolated", s.subnet);
    }
    return kExitOk;
}

} // namespace wirebard
