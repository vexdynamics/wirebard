// commands/peer.cpp — `wirebard peer add|remove`: the imperative, JSON-speaking
// interface baki drives over SSH (docs §5). Each is sugar over the declarative
// spine: author (or delete) a peer partial, then build → check → apply. The
// per-network lock makes concurrent callers safe; a failed apply rolls the
// partial back so the source never diverges from the live interface.
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <system_error>

#include "alloc.h"
#include "apply.h"
#include "check.h"
#include "commands.h"
#include "commands/common.h"
#include "contract.h"
#include "directives.h" // list_partials
#include "fs.h"
#include "json.h"
#include "net.h"
#include "peer.h"
#include "project.h"

namespace wirebard {

namespace fs = std::filesystem;

namespace {

// The network variables a peer operation needs, parsed and validated once.
struct NetVars {
    Subnet subnet;
    uint32_t server_host;
    std::string subnet_text;
    std::string endpoint;
    std::string server_public_key;
    std::string dns;
    std::string mtu;
    bool full_tunnel;
};

Result<NetVars> read_net_vars(const Network& net) {
    auto require = [&](std::string_view key) -> Result<std::string_view> {
        if (auto v = net.vars.get(key))
            return *v;
        return std::unexpected(Error{
            .code = ErrorCode::config,
            .message = std::format("network '{}' defines no '{}' variable", net.paths.name, key)});
    };

    auto subnet_text = require("subnet");
    if (!subnet_text)
        return std::unexpected(subnet_text.error());
    auto endpoint = require("endpoint");
    if (!endpoint)
        return std::unexpected(endpoint.error());
    auto server_pub = require("server_public_key");
    if (!server_pub)
        return std::unexpected(server_pub.error());
    auto address = require("address");
    if (!address)
        return std::unexpected(address.error());

    auto subnet = Subnet::parse(*subnet_text);
    if (!subnet)
        return std::unexpected(subnet.error());
    auto server = parse_cidr(*address);
    if (!server)
        return std::unexpected(server.error());

    return NetVars{.subnet = *subnet,
                   .server_host = server->addr,
                   .subnet_text = std::string(*subnet_text),
                   .endpoint = std::string(*endpoint),
                   .server_public_key = std::string(*server_pub),
                   .dns = std::string(net.vars.get("dns").value_or("")),
                   .mtu = std::string(net.vars.get("mtu").value_or("")),
                   .full_tunnel = net.vars.get("tunnel").value_or("split") == "full"};
}

PeerResult make_result(std::string_view network, const NetVars& nv, uint32_t host) {
    return PeerResult{.network = std::string(network),
                      .full_tunnel = nv.full_tunnel,
                      .address = std::format("{}/{}", format_ipv4(host), nv.subnet.prefix),
                      .subnet = nv.subnet_text,
                      .server_public_key = nv.server_public_key,
                      .endpoint = nv.endpoint,
                      .dns = nv.dns,
                      .mtu = nv.mtu};
}

// Recompile the (freshly reloaded) network, validate, and apply live. Shared by
// add and remove after they mutate the partials.
Result<void> build_check_apply(const ProjectPaths& paths, std::string_view network,
                               std::optional<std::string_view> env) {
    auto net = Network::load(paths, network, env);
    if (!net)
        return std::unexpected(net.error());
    for (const Diagnostic& d : check_network(*net)) {
        if (d.severity == Severity::error)
            return std::unexpected(d.error);
    }
    auto conf = compile_network(*net);
    if (!conf)
        return std::unexpected(conf.error());
    return execute_apply(plan_apply(net->paths, *conf), /*dry_run=*/false);
}

int usage(std::string_view msg) {
    std::println(stderr, "{}", msg);
    return kExitUsage;
}

int fail_env(const Error& e) {
    std::println(stderr, "error: {}", format_error(e));
    return kExitEnvironment;
}

int fail_config(const Error& e) {
    std::println(stderr, "error: {}", format_error(e));
    return kExitFailure;
}

int peer_add(const ParsedArgs& args) {
    auto network = cmd::flag(args, "--network");
    auto pubkey = cmd::flag(args, "--pubkey");
    if (!network || !pubkey)
        return usage("peer add: --network and --pubkey are required");
    if (!is_wireguard_key(*pubkey))
        return usage("peer add: --pubkey must be a base64 WireGuard public key (44 chars)");
    const bool json = args.switches.contains("--json");
    const bool dry = args.switches.contains("--dry-run");
    const std::string label = std::string(cmd::flag(args, "--name").value_or("peer"));

    auto paths = cmd::project_of(args);
    if (!paths)
        return fail_env(paths.error());
    const fs::path netdir = paths->partials_dir / std::string(*network);
    if (!fs::is_directory(netdir))
        return fail_config(Error{.code = ErrorCode::config,
                                 .message = std::format("unknown network '{}'", *network)});

    // Serialize the whole read→allocate→write→apply cycle.
    auto lock = FileLock::acquire(netdir / ".lock");
    if (!lock)
        return fail_env(lock.error());

    auto net = Network::load(*paths, *network, cmd::env_flag(args));
    if (!net)
        return fail_config(net.error());
    auto nv = read_net_vars(*net);
    if (!nv)
        return fail_config(nv.error());
    auto existing = collect_assignments(net->partials);
    if (!existing)
        return fail_config(existing.error());

    // Idempotency: an already-present pubkey keeps its address, no new partial.
    bool existed = false;
    uint32_t host = 0;
    for (const Assignment& a : *existing) {
        if (a.public_key == *pubkey) {
            existed = true;
            host = a.address;
            break;
        }
    }
    if (!existed) {
        auto alloc = allocate_address(nv->subnet, nv->server_host, *existing, *pubkey);
        if (!alloc)
            return fail_config(alloc.error());
        host = *alloc;
    }

    const PeerResult result = make_result(*network, *nv, host);
    auto emit = [&] {
        if (json)
            std::println("{}", peer_add_json(result));
        else
            std::println("peer {} on {}: {} ({} tunnel){}", label, *network, result.address,
                         nv->full_tunnel ? "full" : "split", existed ? " [already present]" : "");
    };

    if (dry) {
        if (existed) {
            std::println(stderr, "[dry-run] pubkey already present as {} — no change",
                         result.address);
        } else {
            auto files = list_partials(netdir);
            const std::string fname =
                files ? next_peer_filename(*files, label) : std::string("NN-<name>.conf");
            std::println(stderr, "[dry-run] would author partials/{}/{} → {}", *network, fname,
                         result.address);
            std::print(stderr, "[dry-run] then:\n{}",
                       describe_plan(plan_apply(net->paths, "<compiled conf — hidden>")));
        }
        emit();
        return kExitOk;
    }

    if (existed) { // already applied on a previous add
        emit();
        return kExitOk;
    }

    // Author the new peer partial, then build → check → apply. On any failure
    // past this point, delete the partial so source and live state stay in sync.
    auto files = list_partials(netdir);
    if (!files)
        return fail_env(files.error());
    const fs::path fpath = netdir / next_peer_filename(*files, label);
    if (auto w = atomic_write_file(fpath, render_peer_partial(label, *pubkey, host)); !w)
        return fail_env(w.error());

    if (auto r = build_check_apply(*paths, *network, cmd::env_flag(args)); !r) {
        std::error_code ec;
        fs::remove(fpath, ec); // rollback
        std::println(stderr, "error: {}", format_error(r.error()));
        // A validation failure is exit 1; anything else (apply/env) is exit 3.
        return r.error().code == ErrorCode::config ? kExitFailure : kExitEnvironment;
    }

    emit();
    return kExitOk;
}

int peer_remove(const ParsedArgs& args) {
    auto network = cmd::flag(args, "--network");
    auto pubkey = cmd::flag(args, "--pubkey");
    if (!network || !pubkey)
        return usage("peer remove: --network and --pubkey are required");
    if (!is_wireguard_key(*pubkey))
        return usage("peer remove: --pubkey must be a base64 WireGuard public key (44 chars)");
    const bool json = args.switches.contains("--json");
    const bool dry = args.switches.contains("--dry-run");

    auto paths = cmd::project_of(args);
    if (!paths)
        return fail_env(paths.error());
    const fs::path netdir = paths->partials_dir / std::string(*network);
    if (!fs::is_directory(netdir))
        return fail_config(Error{.code = ErrorCode::config,
                                 .message = std::format("unknown network '{}'", *network)});

    auto lock = FileLock::acquire(netdir / ".lock");
    if (!lock)
        return fail_env(lock.error());

    auto net = Network::load(*paths, *network, cmd::env_flag(args));
    if (!net)
        return fail_config(net.error());
    auto existing = collect_assignments(net->partials);
    if (!existing)
        return fail_config(existing.error());

    const Assignment* found = nullptr;
    for (const Assignment& a : *existing) {
        if (a.public_key == *pubkey) {
            found = &a;
            break;
        }
    }

    // Not present → idempotent success, {"removed": false}.
    if (found == nullptr) {
        if (json)
            std::println("{}", JsonObject{}.boolean("removed", false).dump());
        else
            std::println("peer not present on {} — nothing to remove", *network);
        return kExitOk;
    }

    const std::string address = format_ipv4(found->address);
    const fs::path fpath = found->source;
    auto removed_json = [&] {
        return JsonObject{}
            .boolean("removed", true)
            .str("network", *network)
            .str("address", address)
            .dump();
    };

    if (dry) {
        std::println(stderr, "[dry-run] would remove {} and reapply {}", fpath.string(), *network);
        if (json)
            std::println("{}", removed_json());
        else
            std::println("peer {} would be removed from {}", address, *network);
        return kExitOk;
    }

    // Keep the partial's bytes so we can restore it if apply fails.
    auto saved = read_file(fpath);
    if (!saved)
        return fail_env(saved.error());
    std::error_code ec;
    fs::remove(fpath, ec);
    if (ec)
        return fail_env(
            Error{.code = ErrorCode::io,
                  .message = std::format("cannot remove {}: {}", fpath.string(), ec.message())});

    if (auto r = build_check_apply(*paths, *network, cmd::env_flag(args)); !r) {
        if (auto rb = atomic_write_file(fpath, *saved); !rb) // rollback the deletion
            std::println(stderr, "warning: could not restore {}: {}", fpath.string(),
                         format_error(rb.error()));
        std::println(stderr, "error: {}", format_error(r.error()));
        return r.error().code == ErrorCode::config ? kExitFailure : kExitEnvironment;
    }

    if (json)
        std::println("{}", removed_json());
    else
        std::println("removed peer {} from {}", address, *network);
    return kExitOk;
}

} // namespace

int cmd_peer(const ParsedArgs& args) {
    const std::string sub = args.positionals.empty() ? "" : args.positionals.front();
    if (sub == "add")
        return peer_add(args);
    if (sub == "remove")
        return peer_remove(args);
    return usage("usage: wirebard peer add|remove --network N --pubkey K [--name L] [--json]");
}

} // namespace wirebard
