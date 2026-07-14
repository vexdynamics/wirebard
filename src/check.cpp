#include "check.h"

#include <format>
#include <optional>
#include <set>

#include "net.h"
#include "peer.h"

namespace wirebard {

namespace {

Diagnostic error_at(std::string message, std::filesystem::path file = {}, int line = 0) {
    Error e{.code = ErrorCode::config, .message = std::move(message)};
    if (!file.empty())
        e.where = SourceLoc{.file = std::move(file), .line = line};
    return Diagnostic{.severity = Severity::error, .error = std::move(e)};
}

bool has_interface_section(std::span<const Partial> partials) {
    for (const Partial& p : partials) {
        std::string_view text = p.raw;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos)
                eol = text.size();
            std::string_view line = text.substr(pos, eol - pos);
            pos = eol + 1;
            // trim leading blanks
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.remove_prefix(1);
            if (line.starts_with("[Interface]") || line.starts_with("[interface]"))
                return true;
        }
    }
    return false;
}

} // namespace

std::vector<Diagnostic> check_network(const Network& net) {
    std::vector<Diagnostic> diags;

    if (!has_interface_section(net.partials)) {
        diags.push_back(
            error_at("no [Interface] section — 00-main.conf must define one", net.paths.dir));
    }

    // subnet: required to place peers.
    std::optional<Subnet> subnet;
    if (auto s = net.vars.get("subnet")) {
        if (auto parsed = Subnet::parse(*s))
            subnet = *parsed;
        else
            diags.push_back(error_at(std::format("bad 'subnet' value: {}", parsed.error().message),
                                     net.paths.dir));
    } else {
        diags.push_back(error_at("network defines no 'subnet' variable (needed to allocate peers)",
                                 net.paths.dir));
    }

    // address: the server's own interface address.
    std::optional<uint32_t> server;
    if (auto a = net.vars.get("address")) {
        if (auto c = parse_cidr(*a))
            server = c->addr;
        else
            diags.push_back(
                error_at(std::format("bad 'address' value: {}", c.error().message), net.paths.dir));
    } else {
        diags.push_back(error_at("network defines no 'address' variable (the server's own address)",
                                 net.paths.dir));
    }

    // Peers. collect_assignments stops at the first structurally-broken peer;
    // surface that as one diagnostic and skip the per-peer checks it blocks.
    auto assigns = collect_assignments(net.partials);
    if (!assigns) {
        diags.push_back(Diagnostic{.severity = Severity::error, .error = assigns.error()});
        return diags;
    }

    std::set<std::string> seen_keys;
    std::set<uint32_t> seen_addrs;
    for (const Assignment& a : *assigns) {
        const std::string label = a.name.empty() ? a.public_key : a.name;

        if (!seen_keys.insert(a.public_key).second)
            diags.push_back(error_at("duplicate PublicKey (also on another peer)", a.source));
        if (!seen_addrs.insert(a.address).second)
            diags.push_back(error_at(std::format("duplicate address {} (also assigned elsewhere)",
                                                 format_ipv4(a.address)),
                                     a.source));
        if (subnet && !subnet->contains(a.address))
            diags.push_back(error_at(std::format("peer '{}' address {} is outside subnet {}/{}",
                                                 label, format_ipv4(a.address),
                                                 format_ipv4(subnet->network), subnet->prefix),
                                     a.source));
        if (subnet && (a.address == subnet->network || a.address == subnet->broadcast()))
            diags.push_back(error_at(std::format("peer '{}' uses the network/broadcast address {}",
                                                 label, format_ipv4(a.address)),
                                     a.source));
        if (server && a.address == *server)
            diags.push_back(error_at(std::format("peer '{}' collides with the server address {}",
                                                 label, format_ipv4(*server)),
                                     a.source));
    }

    return diags;
}

} // namespace wirebard
