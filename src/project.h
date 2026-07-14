// project.h — locating a wirebard project and loading one network from it.
// Commands stay thin; the pipeline (find files → load vars → substitute
// partials) lives here, in one place. Ported from haladin's project facade,
// generalized from "one config" to "one interface per network".
//
// A project directory mirrors /etc/wireguard on a real server — back up (or
// rsync) the directory, run `wirebard build` on the other side, and the
// server is recreated:
//
//     <root>/                    ↔  /etc/wireguard/
//     ├── backups.conf              compiled interface (what wg-quick reads)
//     ├── roam.conf
//     └── partials/
//         ├── backups/             one folder per network (= per interface)
//         │   ├── server.key        0600 private key; never emitted
//         │   ├── 00-main.conf      [Interface] + shared #= variables
//         │   └── NN-<name>.conf    one [Peer] per file, merged in order
//         └── roam/
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"
#include "partial.h"
#include "vars.h"

namespace wirebard {

// Where is the project root? Explicit -C wins; otherwise the current directory
// if it has a partials/ subdir (dev workflow); otherwise /etc/wireguard (the
// default — WireGuard's own config folder, so commands just work on a server).
// Every command funnels through this, so the rule exists exactly once.
Result<std::filesystem::path>
resolve_project_root(const std::optional<std::filesystem::path>& cli_dir);

struct ProjectPaths {
    std::filesystem::path root;
    std::filesystem::path partials_dir; // root/partials

    // Validates that partials/ exists; the error spells out what a wirebard
    // project looks like (first-run experience matters).
    static Result<ProjectPaths> locate(const std::filesystem::path& dir);
};

// One network's on-disk locations. The folder name is the network name is the
// interface name (wg-quick@<name>).
struct NetworkPaths {
    std::string name;
    std::filesystem::path dir;        // root/partials/<name>
    std::filesystem::path output;     // root/<name>.conf  (build/apply target)
    std::filesystem::path server_key; // root/partials/<name>/server.key
};

// Every network under the root: each partials/<name>/ folder that holds at
// least one compiled partial. Sorted by name (directory order is unspecified).
// Folders with no compiled partial (empty, or only template.conf) are skipped,
// not errors — they may be scaffolding.
Result<std::vector<NetworkPaths>> list_networks(const ProjectPaths& project);

// One fully-loaded network: paths + resolved vars + substituted partials in
// merge order. The facade every network command starts from.
struct Network {
    NetworkPaths paths;
    std::optional<std::string> env; // the --env name, if any
    VarTable vars;
    std::vector<Partial> partials; // 00-main first, then NN- order

    static Result<Network> load(const ProjectPaths& project, std::string_view name,
                                std::optional<std::string_view> env);
};

} // namespace wirebard
