// alloc.h — assign an address to a peer, deterministically.
//
// The heart of wirebard's statefulness, kept pure so it can be tested to
// death. Given the subnet, the server's own address, and every existing
// assignment, allocate() answers one question: what /32 does this pubkey get?
//
// Two guarantees callers rely on:
//   * Idempotent — a pubkey already assigned returns its SAME address, never a
//     second one. (Re-running `peer add` must be a no-op.)
//   * Deterministic — a brand-new pubkey gets the LOWEST free host in the
//     subnet, excluding the server and every taken address. No randomness, no
//     clock. Same inputs, same answer.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "error.h"
#include "net.h"
#include "peer.h"

namespace wirebard {

// The address to assign `public_key` within `subnet`. If `public_key` is
// already in `existing`, returns that address unchanged. Otherwise the lowest
// free host, excluding `server_address` and every address in `existing`. A
// full subnet is ErrorCode::config (loud failure, never a silent reuse).
Result<uint32_t> allocate_address(const Subnet& subnet, uint32_t server_address,
                                  std::span<const Assignment> existing,
                                  std::string_view public_key);

} // namespace wirebard
