#include "net.h"

#include <gtest/gtest.h>

namespace {

uint32_t ip(std::string_view s) {
    auto r = wirebard::parse_ipv4(s);
    EXPECT_TRUE(r.has_value()) << "failed to parse " << s;
    return r.value_or(0);
}

TEST(NetTest, ParsesAndRoundTripsIPv4) {
    EXPECT_EQ(wirebard::parse_ipv4("0.0.0.0"), 0u);
    EXPECT_EQ(wirebard::parse_ipv4("255.255.255.255"), 0xFFFFFFFFu);
    EXPECT_EQ(wirebard::parse_ipv4("10.8.2.1"), 0x0A080201u);
    EXPECT_EQ(wirebard::format_ipv4(0x0A080201u), "10.8.2.1");
    EXPECT_EQ(wirebard::format_ipv4(ip("192.168.0.255")), "192.168.0.255");
}

TEST(NetTest, RejectsMalformedIPv4) {
    for (std::string_view bad : {"", "10.8.2", "10.8.2.1.5", "10.8.2.256", "10.8.2.-1", "10.8.2.",
                                 ".8.2.1", "10.8.2.x", "10.8.2.01", "300.1.1.1"}) {
        EXPECT_FALSE(wirebard::parse_ipv4(bad).has_value()) << "should reject '" << bad << "'";
    }
}

TEST(NetTest, ParsesCidrKeepingOriginalAddress) {
    auto c = wirebard::parse_cidr("10.8.2.1/24");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->addr, ip("10.8.2.1")); // NOT masked
    EXPECT_EQ(c->prefix, 24);
    EXPECT_FALSE(wirebard::parse_cidr("10.8.2.1").has_value());    // no prefix
    EXPECT_FALSE(wirebard::parse_cidr("10.8.2.1/33").has_value()); // out of range
    EXPECT_FALSE(wirebard::parse_cidr("10.8.2.1/04").has_value()); // leading zero
}

TEST(NetTest, SubnetMasksNetworkAndComputesRange) {
    auto s = wirebard::Subnet::parse("10.8.2.5/24"); // host bits present, get masked
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(wirebard::format_ipv4(s->network), "10.8.2.0");
    EXPECT_EQ(wirebard::format_ipv4(s->broadcast()), "10.8.2.255");
    EXPECT_EQ(wirebard::format_ipv4(s->first_host()), "10.8.2.1");
    EXPECT_EQ(wirebard::format_ipv4(s->last_host()), "10.8.2.254");
    EXPECT_TRUE(s->contains(ip("10.8.2.200")));
    EXPECT_FALSE(s->contains(ip("10.8.3.1")));
}

TEST(NetTest, DegenerateSubnetsHaveNoUsableHosts) {
    auto s31 = wirebard::Subnet::parse("10.8.2.0/31");
    ASSERT_TRUE(s31.has_value());
    EXPECT_GT(s31->first_host(), s31->last_host()); // empty range

    auto s32 = wirebard::Subnet::parse("10.8.2.5/32");
    ASSERT_TRUE(s32.has_value());
    EXPECT_GT(s32->first_host(), s32->last_host());

    auto s0 = wirebard::Subnet::parse("0.0.0.0/0");
    ASSERT_TRUE(s0.has_value());
    EXPECT_EQ(s0->netmask(), 0u);
    EXPECT_EQ(s0->broadcast(), 0xFFFFFFFFu);
}

} // namespace
