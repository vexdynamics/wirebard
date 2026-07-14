#include "project.h"

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

#include "directives.h"

namespace wirebard {

namespace {

namespace fs = std::filesystem;

// Assemble a network's paths from the project root and a name. No filesystem
// access — pure path arithmetic, so both list_networks and Network::load
// derive locations identically.
NetworkPaths network_paths(const ProjectPaths& project, std::string name) {
    fs::path dir = project.partials_dir / name;
    fs::path output = project.root / (name + std::string(kPartialSuffix));
    fs::path server_key = dir / kServerKeyName;
    return NetworkPaths{.name = std::move(name),
                        .dir = std::move(dir),
                        .output = std::move(output),
                        .server_key = std::move(server_key)};
}

} // namespace

Result<fs::path> resolve_project_root(const std::optional<fs::path>& cli_dir) {
    if (cli_dir)
        return *cli_dir; // explicit -C: trust it, locate() validates next

    if (fs::is_directory(fs::path(".") / kPartialsDir))
        return fs::path(".");

    // Not const: returning a const local forces a copy where a move would do
    // (clang-tidy: performance-no-automatic-move).
    fs::path etc_wireguard = "/etc/wireguard";
    if (fs::is_directory(etc_wireguard / kPartialsDir))
        return etc_wireguard;

    return std::unexpected(
        Error{.code = ErrorCode::io,
              .message = std::format("no wirebard project found — run inside a directory "
                                     "containing {0}/, create /etc/wireguard/{0}/, or pass -C DIR",
                                     kPartialsDir)});
}

Result<ProjectPaths> ProjectPaths::locate(const fs::path& dir) {
    if (!fs::is_directory(dir)) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("'{}' is not a directory (use -C to point at a project)",
                                         dir.string()),
                  .where = SourceLoc{.file = dir}});
    }

    ProjectPaths p{.root = dir, .partials_dir = dir / kPartialsDir};

    if (!fs::is_directory(p.partials_dir)) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("'{}' is not a wirebard project: missing {}/ — a project "
                                         "mirrors /etc/wireguard and keeps each network's sources "
                                         "in {}/<network>/*{} files",
                                         dir.string(), kPartialsDir, kPartialsDir, kPartialSuffix),
                  .where = SourceLoc{.file = dir}});
    }
    return p;
}

Result<std::vector<NetworkPaths>> list_networks(const ProjectPaths& project) {
    std::error_code ec;
    fs::directory_iterator it(project.partials_dir, ec);
    if (ec) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("cannot read {}/: {}", kPartialsDir, ec.message()),
                  .where = SourceLoc{.file = project.partials_dir}});
    }

    std::vector<NetworkPaths> out;
    for (const auto& entry : it) {
        if (!entry.is_directory())
            continue;
        // A folder is a network iff it holds at least one compiled partial.
        auto parts = list_partials(entry.path());
        if (!parts || parts->empty())
            continue;
        out.push_back(network_paths(project, entry.path().filename().string()));
    }

    // directory_iterator order is unspecified — sort by name so `network list`
    // and every consumer is deterministic.
    std::ranges::sort(out, {}, &NetworkPaths::name);
    return out;
}

Result<Network> Network::load(const ProjectPaths& project, std::string_view name,
                              std::optional<std::string_view> env) {
    NetworkPaths np = network_paths(project, std::string(name));
    if (!fs::is_directory(np.dir)) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("unknown network '{}' — no {}/{}/ folder "
                                         "(see `wirebard network list`)",
                                         name, kPartialsDir, name),
                  .where = SourceLoc{.file = np.dir}});
    }

    auto vars = VarTable::load(np.dir, env);
    if (!vars)
        return std::unexpected(vars.error());

    auto partials = load_partials(np.dir, *vars);
    if (!partials)
        return std::unexpected(partials.error());

    return Network{.paths = std::move(np),
                   .env = env ? std::optional<std::string>(*env) : std::nullopt,
                   .vars = std::move(*vars),
                   .partials = std::move(*partials)};
}

} // namespace wirebard
