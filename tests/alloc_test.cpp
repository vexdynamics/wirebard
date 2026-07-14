#include "alloc.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "net.h"
#include "peer.h"

namespace {

wirebard::Subnet net(std::string_view s) { return wirebard::Subnet::parse(s).value(); }
uint32_t ip(std::string_view s) { return wirebard::parse_ipv4(s).value(); }

wirebard::Assignment assign(std::string key, std::string_view addr) {
    return wirebard::Assignment{
        .public_key = std::move(key), .name = "", .address = ip(addr), .source = {}};
}

// Allocate and return the dotted-quad, for readable expectations.
std::string alloc(const wirebard::Subnet& subnet, std::string_view server,
                  std::span<const wirebard::Assignment> existing, std::string_view pubkey) {
    auto r = wirebard::allocate_address(subnet, ip(server), existing, pubkey);
    EXPECT_TRUE(r.has_value()) << (r ? "" : wirebard::format_error(r.error()));
    return r ? wirebard::format_ipv4(*r) : "<error>";
}

TEST(AllocTest, FirstFreeHostInAnEmptySubnetSkipsTheServer) {
    // Server is .1, so a fresh peer lands on .2.
    EXPECT_EQ(alloc(net("10.8.2.0/24"), "10.8.2.1", {}, "NEW"), "10.8.2.2");
}

TEST(AllocTest, LowestFreeSkipsServerAndTakenAddresses) {
    std::vector<wirebard::Assignment> existing = {
        assign("A", "10.8.2.2"),
        assign("B", "10.8.2.4"),
    };
    // Server .1, taken .2 and .4 → lowest gap is .3.
    EXPECT_EQ(alloc(net("10.8.2.0/24"), "10.8.2.1", existing, "NEW"), "10.8.2.3");
}

TEST(AllocTest, IsIdempotentForAKnownPubkey) {
    std::vector<wirebard::Assignment> existing = {
        assign("A", "10.8.2.2"),
        assign("B", "10.8.2.9"),
    };
    // Re-adding B returns B's SAME address, never a new one.
    EXPECT_EQ(alloc(net("10.8.2.0/24"), "10.8.2.1", existing, "B"), "10.8.2.9");
    // ...even when a lower address (.3) is free.
    EXPECT_EQ(alloc(net("10.8.2.0/24"), "10.8.2.1", existing, "A"), "10.8.2.2");
}

TEST(AllocTest, DeterministicRegardlessOfExistingOrder) {
    std::vector<wirebard::Assignment> a = {assign("A", "10.8.2.2"), assign("B", "10.8.2.3")};
    std::vector<wirebard::Assignment> b = {assign("B", "10.8.2.3"), assign("A", "10.8.2.2")};
    EXPECT_EQ(alloc(net("10.8.2.0/24"), "10.8.2.1", a, "NEW"),
              alloc(net("10.8.2.0/24"), "10.8.2.1", b, "NEW"));
}

TEST(AllocTest, FullSubnetIsALoudError) {
    // /29 = 8 addresses, usable hosts .1..6. Server takes .1; fill .2..6.
    std::vector<wirebard::Assignment> existing;
    for (int last = 2; last <= 6; ++last)
        existing.push_back(
            assign(std::string(1, char('A' + last)), "10.8.2." + std::to_string(last)));
    auto r = wirebard::allocate_address(net("10.8.2.0/29"), ip("10.8.2.1"), existing, "NEW");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::config);

    // ...but re-adding an EXISTING peer still succeeds even when full.
    auto known = wirebard::allocate_address(net("10.8.2.0/29"), ip("10.8.2.1"), existing,
                                            existing.front().public_key);
    ASSERT_TRUE(known.has_value());
    EXPECT_EQ(*known, existing.front().address);
}

TEST(AllocTest, DegenerateSubnetHasNoHosts) {
    auto r = wirebard::allocate_address(net("10.8.2.0/31"), ip("10.8.2.0"), {}, "NEW");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::config);
}

} // namespace
