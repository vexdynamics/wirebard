// apply.h — install a compiled interface config and bring it live, the one
// place in wirebard that mutates the running system. Everything here is at the
// OS boundary (files + wg/wg-quick/systemctl), so it is deliberately thin and
// leans on a --dry-run seam: the plan can be printed without executing, which
// is how the un-rootable parts get reviewed.
#pragma once

#include <filesystem>
#include <string>

#include "error.h"
#include "project.h"

namespace wirebard {

// What `apply` will do for one network. conf_contents is held apart and NEVER
// printed — it carries the server PrivateKey (see describe_plan).
struct ApplyPlan {
    std::filesystem::path conf_path; // /etc/wireguard/<name>.conf, written 0600
    std::string conf_contents;       // compiled interface config (with PrivateKey)
    std::string interface;           // <name>: the systemd unit + wg interface
};

// Pure: where the conf goes and what interface it is. (The wg/systemctl steps
// are fixed and live in execute_apply/describe_plan.)
ApplyPlan plan_apply(const NetworkPaths& paths, std::string compiled_conf);

// Read the network's server.key and compile its partials into the interface
// config, with the private key injected under [Interface]. The one function
// that pairs the secret with the compiled output; the result is destined for a
// 0600 file only. Errors (environment) if server.key is missing.
Result<std::string> compile_network(const Network& net);

// A human-readable, SECRET-FREE description of the steps — used for --dry-run
// output. Mentions the conf path but never its contents.
std::string describe_plan(const ApplyPlan& plan);

// Execute the plan: write the 0600 conf, `systemctl enable` the unit (persist
// across reboot), then reload live — `wg syncconf` when the interface is
// already up (so peers already connected are NOT dropped), or `systemctl start`
// to bootstrap it. Stops at the first failure. When dry_run, prints
// describe_plan() and touches nothing.
Result<void> execute_apply(const ApplyPlan& plan, bool dry_run);

} // namespace wirebard
