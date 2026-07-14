#include "alloc.h"

#include <format>
#include <set>

namespace wirebard {

Result<uint32_t> allocate_address(const Subnet& subnet, uint32_t server_address,
                                  std::span<const Assignment> existing,
                                  std::string_view public_key) {
    // Idempotent: an already-assigned pubkey keeps its exact address.
    for (const Assignment& a : existing) {
        if (a.public_key == public_key)
            return a.address;
    }

    if (subnet.prefix >= 31) {
        return std::unexpected(Error{
            .code = ErrorCode::config,
            .message = std::format("subnet /{} has no allocatable host addresses", subnet.prefix)});
    }

    // The taken set: the server's own address plus every existing peer's.
    std::set<uint32_t> used;
    used.insert(server_address);
    for (const Assignment& a : existing)
        used.insert(a.address);

    // Walk hosts low → high; first gap wins. The `h == last` break (rather than
    // `h <= last` in the condition) avoids the uint32 wrap when last is the top
    // of the range.
    const uint32_t first = subnet.first_host();
    const uint32_t last = subnet.last_host();
    for (uint32_t h = first;; ++h) {
        if (!used.contains(h))
            return h;
        if (h == last)
            break;
    }

    return std::unexpected(
        Error{.code = ErrorCode::config,
              .message = std::format("subnet {}/{} is full — no free address to assign",
                                     format_ipv4(subnet.network), subnet.prefix)});
}

} // namespace wirebard
