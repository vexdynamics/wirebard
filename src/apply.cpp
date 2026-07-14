#include "apply.h"

#include <format>
#include <print>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "fs.h"
#include "render.h"
#include "subprocess.h"

namespace wirebard {

namespace fs = std::filesystem;

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// Run argv; a non-zero exit becomes an Error (the caller wanted it to succeed).
Result<CommandResult> run_checked(std::vector<std::string> argv, std::string_view what) {
    auto r = run_command(argv);
    if (!r)
        return std::unexpected(r.error()); // failed to even run
    if (!r->ok()) {
        return std::unexpected(Error{
            .code = ErrorCode::subprocess,
            .message = std::format("{} failed (exit {}): {}", what, r->exit_code, trim(r->err))});
    }
    return r;
}

} // namespace

ApplyPlan plan_apply(const NetworkPaths& paths, std::string compiled_conf) {
    return ApplyPlan{.conf_path = paths.output,
                     .conf_contents = std::move(compiled_conf),
                     .interface = paths.name};
}

Result<std::string> compile_network(const Network& net) {
    auto key = read_file(net.paths.server_key);
    if (!key) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("cannot read server key {} — generate one with "
                                         "`wg genkey > server.key && chmod 600 server.key`",
                                         net.paths.server_key.string()),
                  .where = SourceLoc{.file = net.paths.server_key}});
    }
    const std::string_view private_key = trim(*key);
    const std::string_view env = net.env ? std::string_view{*net.env} : std::string_view{};
    return compile(
        net.partials,
        {.interface = net.paths.name, .env_label = env, .server_private_key = private_key});
}

std::string describe_plan(const ApplyPlan& plan) {
    const std::string unit = std::format("wg-quick@{}", plan.interface);
    std::string out;
    out += std::format("write {} (mode 0600, {} bytes; contents hidden — carries the server key)\n",
                       plan.conf_path.string(), plan.conf_contents.size());
    out += std::format("run:  systemctl enable {}\n", unit);
    out +=
        std::format("run:  wg show {}   (probe: is the interface already up?)\n", plan.interface);
    out += std::format("  if up:   wg-quick strip {0} → wg syncconf {0}   (reload, no peer drop)\n",
                       plan.interface);
    out += std::format("  if down: systemctl start {}\n", unit);
    return out;
}

Result<void> execute_apply(const ApplyPlan& plan, bool dry_run) {
    const std::string unit = std::format("wg-quick@{}", plan.interface);

    if (dry_run) {
        std::print("{}", describe_plan(plan));
        return {};
    }

    // 1. Install the compiled config, 0600 (it carries the private key).
    if (auto w = atomic_write_file(plan.conf_path, plan.conf_contents,
                                   fs::perms::owner_read | fs::perms::owner_write);
        !w)
        return std::unexpected(w.error());

    // 2. Persist across reboot. Idempotent; no effect on the running interface.
    if (auto r = run_checked({"systemctl", "enable", unit}, "systemctl enable"); !r)
        return std::unexpected(r.error());

    // 3. Reload live. `wg show <iface>` probes whether the interface exists.
    bool up = false;
    if (auto probe = run_command(std::vector<std::string>{"wg", "show", plan.interface}); probe)
        up = probe->ok();
    else
        return std::unexpected(probe.error());

    if (up) {
        // Reconcile the live interface to the new config WITHOUT dropping peers.
        // `wg syncconf` needs wg-format (no Address/DNS/PostUp), which
        // `wg-quick strip` produces from the installed conf.
        auto strip = run_checked({"wg-quick", "strip", plan.interface}, "wg-quick strip");
        if (!strip)
            return std::unexpected(strip.error());

        // The stripped config still carries the private key → temp file 0600,
        // removed right after. Never printed.
        fs::path sync_tmp = plan.conf_path;
        sync_tmp += ".sync";
        if (auto w = atomic_write_file(sync_tmp, strip->out,
                                       fs::perms::owner_read | fs::perms::owner_write);
            !w)
            return std::unexpected(w.error());

        auto sync =
            run_checked({"wg", "syncconf", plan.interface, sync_tmp.string()}, "wg syncconf");
        std::error_code ec;
        fs::remove(sync_tmp, ec); // best-effort; the secret must not linger
        if (!sync)
            return std::unexpected(sync.error());
    } else {
        // First time up (or after a reboot): start brings it live from the conf.
        if (auto r = run_checked({"systemctl", "start", unit}, "systemctl start"); !r)
            return std::unexpected(r.error());
    }

    return {};
}

} // namespace wirebard
