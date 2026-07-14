// render.h — merge a network's partials into the interface config wg-quick
// reads. Pure and deterministic: identical inputs produce byte-identical
// output, so "diff the generated config before deploying" is meaningful — any
// byte that changed, YOU changed.
#pragma once

#include <span>
#include <string>
#include <string_view>

#include "partial.h"

namespace wirebard {

struct CompileOptions {
    std::string_view interface; // network/interface name, for the banner
    std::string_view env_label; // "" when no --env is active
    // The server's private key, injected as the first line under [Interface].
    // Empty renders WITHOUT it — for `check`/dry runs. The real key lives only
    // in the compiled 0600 file, never in a partial, --verbose, or a client
    // config (see CLAUDE.md's secrets rules).
    std::string_view server_private_key;
};

// Banner + each partial verbatim (in the given, already-sorted order), each
// preceded by a "# ===== 10-alice.conf =====" marker so a human reading the
// output can trace every line to its source partial.
std::string compile(std::span<const Partial> partials, const CompileOptions& opts);

} // namespace wirebard
