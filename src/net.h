// net.h — IPv4 address and subnet arithmetic. Pure integer math, no I/O; this
// is the numeric core the allocator (and later the client-config renderer)
// stand on, so it is unit-tested exhaustively.
//
// Addresses are 32-bit host-order integers throughout (10.0.0.1 == 0x0A000001).
// WireGuard configs are IPv4/IPv6, but wirebard allocates from IPv4 subnets;
// IPv6 peers, if ever needed, ride literally in hand-written partials.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "error.h"

namespace wirebard {

// Parse a dotted-quad ("10.8.2.1") into a host-order integer. Rejects empty
// fields, non-digits, octets > 255, the wrong field count, and multi-digit
// octets with a leading zero (which inet_aton would read as octal — a silent
// surprise we refuse loudly).
Result<uint32_t> parse_ipv4(std::string_view s);

// The dotted-quad text of a host-order integer.
std::string format_ipv4(uint32_t addr);

// "addr/prefix" with the ORIGINAL address kept (not masked to the network) —
// used for the server's own `address` var, e.g. "10.8.2.1/24" -> {addr for
// 10.8.2.1, prefix 24}. prefix must be 0..32.
struct Cidr {
    uint32_t addr;
    int prefix;
};
Result<Cidr> parse_cidr(std::string_view s);

// An IPv4 network: base address (masked to the prefix) + prefix length.
struct Subnet {
    uint32_t network; // already masked: "10.8.2.5/24" and "10.8.2.0/24" match
    int prefix;

    // Parse "10.8.2.0/24". Masks the address to the prefix.
    static Result<Subnet> parse(std::string_view s);

    [[nodiscard]] uint32_t netmask() const;
    [[nodiscard]] uint32_t broadcast() const; // network with an all-ones host part
    [[nodiscard]] bool contains(uint32_t addr) const;

    // Inclusive usable-host range: network+1 .. broadcast-1. For prefix >= 31
    // there are no allocatable hosts and first_host() > last_host() (an empty
    // range) — callers treat that as exhaustion.
    [[nodiscard]] uint32_t first_host() const;
    [[nodiscard]] uint32_t last_host() const;
};

} // namespace wirebard
