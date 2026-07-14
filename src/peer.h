// peer.h — extract the peers already recorded in a network's partials.
//
// A peer partial is a [Peer] block: a PublicKey and an AllowedIPs the server
// routes to that peer (its assigned /32). Its label rides in a leading
// "# wirebard: name=<label>" comment WireGuard ignores. This module reads that
// back out of the substituted partial text so the allocator knows which
// addresses are taken and which pubkeys are already assigned (idempotency).
//
// It is a MINIMAL, tolerant scanner — enough to find the two fields it needs,
// treating everything else as opaque. Full config validation is `check`'s job
// (and ultimately wg's), not this module's.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "error.h"
#include "partial.h"

namespace wirebard {

// One peer's recorded assignment.
struct Assignment {
    std::string public_key;
    std::string name;             // "" when no "# wirebard: name=" comment
    uint32_t address;             // host from AllowedIPs's first entry
    std::filesystem::path source; // the partial it came from, for diagnostics
};

// Every [Peer] across the given partials, in encounter order. A [Peer] with a
// PublicKey but no valid AllowedIPs (or vice versa) is a loud error — a
// half-specified peer must never be silently ignored. The main partial's
// [Interface] and any section that is neither contributes nothing.
Result<std::vector<Assignment>> collect_assignments(std::span<const Partial> partials);

// True if `s` is shaped like a WireGuard key: base64 of 32 bytes, i.e. exactly
// 44 characters from the base64 alphabet ending in a single '='. Cheap
// structural check — it rejects empty/garbage input at the door (the contract
// must not trust callers); wg itself is the final arbiter of a real key.
bool is_wireguard_key(std::string_view s);

// --- authoring (what `peer add` writes) -----------------------------------

// The text of a new peer partial: the "# wirebard: name=" metadata comment plus
// a [Peer] block. `address` is emitted as a /32 — exactly the host the server
// routes to this peer. Pure.
std::string render_peer_partial(std::string_view name, std::string_view public_key,
                                uint32_t address);

// Filename for a new peer partial: the next free NN- order prefix (10 past the
// highest present, or 10 if none) plus a filesystem-safe form of `name`.
// `existing` is the network's current partial paths (list_partials order).
std::string next_peer_filename(std::span<const std::filesystem::path> existing,
                               std::string_view name);

} // namespace wirebard
