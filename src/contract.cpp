#include "contract.h"

#include <format>

#include "json.h"

namespace wirebard {

namespace {

// The contract's `type` field name for a tunnel mode (baki's vocabulary; the
// #= var uses split/full — see docs §4).
std::string_view type_name(bool full_tunnel) { return full_tunnel ? "proxy" : "isolated"; }

} // namespace

std::string render_client_config(const PeerResult& r) {
    std::string out;
    out += "[Interface]\n";
    out += "PrivateKey = {{PRIVATE_KEY}}\n";
    out += std::format("Address = {}\n", r.address);
    if (!r.dns.empty())
        out += std::format("DNS = {}\n", r.dns);
    if (!r.mtu.empty())
        out += std::format("MTU = {}\n", r.mtu);
    out += "\n[Peer]\n";
    out += std::format("PublicKey = {}\n", r.server_public_key);
    out += std::format("Endpoint = {}\n", r.endpoint);
    // The whole point of wirebard rendering this: full tunnel routes everything,
    // split tunnel reaches only the network's subnet.
    out += std::format("AllowedIPs = {}\n", r.full_tunnel ? "0.0.0.0/0, ::/0" : r.subnet);
    return out;
}

std::string peer_add_json(const PeerResult& r) {
    return JsonObject{}
        .str("network", r.network)
        .str("type", type_name(r.full_tunnel))
        .str("address", r.address)
        .str("server_public_key", r.server_public_key)
        .str("endpoint", r.endpoint)
        .str("client_config", render_client_config(r))
        .dump();
}

std::string network_list_json(std::span<const NetworkSummary> networks) {
    JsonArray arr;
    for (const NetworkSummary& n : networks) {
        arr.push(JsonObject{}
                     .str("name", n.name)
                     .str("type", type_name(n.full_tunnel))
                     .str("subnet", n.subnet)
                     .dump());
    }
    return arr.dump();
}

} // namespace wirebard
