// contract.h — the caller-facing outputs: the rendered client config and the
// JSON envelopes for `peer add` and `network list`. Pure string-building over
// structured inputs, so the exact bytes the caller receives are unit-tested
// here; the commands (M6) just gather the inputs and print the result.
//
// See docs/design/peer-provisioning.md §4/§7 for the contract this implements.
#pragma once

#include <span>
#include <string>

namespace wirebard {

// Everything needed to render one peer's client config and its JSON envelope.
// `full_tunnel` is the network's `tunnel = full` policy — the ONLY thing that
// differs between network kinds, and the reason wirebard (not the caller)
// renders this: only wirebard knows the AllowedIPs policy.
struct PeerResult {
    std::string network;
    bool full_tunnel;    // true → type "proxy" / AllowedIPs 0.0.0.0/0; false → "isolated"
    std::string address; // client Address, host + subnet prefix, e.g. "10.8.2.5/24"
    std::string subnet;  // network subnet, split-tunnel AllowedIPs, e.g. "10.8.2.0/24"
    std::string server_public_key;
    std::string endpoint; // "vpn.example.com:51820"
    std::string dns;      // "" to omit the DNS line
    std::string mtu;      // "" to omit the MTU line
};

// A complete wg-quick client config with exactly one placeholder,
// {{PRIVATE_KEY}}, for the caller to substitute. Never contains real secret
// material — the caller generated its own keypair locally.
std::string render_client_config(const PeerResult& r);

// The `peer add` JSON envelope: {network, type, address, server_public_key,
// endpoint, client_config}. Exactly one object; the command prints it alone on
// stdout.
std::string peer_add_json(const PeerResult& r);

// One row of `network list`.
struct NetworkSummary {
    std::string name;
    bool full_tunnel;
    std::string subnet;
};

// The `network list` JSON envelope: [{name, type, subnet}, ...].
std::string network_list_json(std::span<const NetworkSummary> networks);

} // namespace wirebard
