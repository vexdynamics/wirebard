#include "peer.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "net.h"
#include "partial.h"

namespace {

// Build a Partial straight from text — collect_assignments works on the
// already-substituted `raw`, so tests skip the filesystem entirely.
wirebard::Partial part(std::string path, std::string raw) {
    return wirebard::Partial{.path = std::move(path), .raw = std::move(raw)};
}

TEST(PeerTest, CollectsPeersWithNamesAndAddresses) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(
        part("00-main.conf", "[Interface]\nAddress = 10.8.2.1/24\nListenPort = 51820\n"));
    parts.push_back(part("10-alice.conf", "# wirebard: name=alice\n[Peer]\n"
                                          "PublicKey = AAA\nAllowedIPs = 10.8.2.2/32\n"));
    parts.push_back(part("20-bob.conf", "# wirebard: name=bob\n[Peer]\n"
                                        "PublicKey = BBB\nAllowedIPs = 10.8.2.3/32\n"));

    auto got = wirebard::collect_assignments(parts);
    ASSERT_TRUE(got.has_value()) << wirebard::format_error(got.error());
    ASSERT_EQ(got->size(), 2u); // [Interface] contributes nothing
    EXPECT_EQ((*got)[0].name, "alice");
    EXPECT_EQ((*got)[0].public_key, "AAA");
    EXPECT_EQ(wirebard::format_ipv4((*got)[0].address), "10.8.2.2");
    EXPECT_EQ((*got)[1].name, "bob");
    EXPECT_EQ(wirebard::format_ipv4((*got)[1].address), "10.8.2.3");
}

TEST(PeerTest, MultiplePeersInOneFileAndCommaAllowedIPs) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("peers.conf",
                         "[Peer]\nPublicKey = AAA\nAllowedIPs = 10.8.2.2/32, 10.8.9.0/24\n"
                         "[Peer]\nPublicKey = BBB\nAllowedIPs = 10.8.2.7\n"));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_TRUE(got.has_value()) << wirebard::format_error(got.error());
    ASSERT_EQ(got->size(), 2u);
    EXPECT_EQ(wirebard::format_ipv4((*got)[0].address), "10.8.2.2"); // first entry only
    EXPECT_EQ(wirebard::format_ipv4((*got)[1].address), "10.8.2.7"); // bare host, no /32
}

TEST(PeerTest, PeerMissingAllowedIPsIsAnError) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("10-x.conf", "[Peer]\nPublicKey = AAA\n"));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_FALSE(got.has_value());
    EXPECT_EQ(got.error().code, wirebard::ErrorCode::config);
    EXPECT_EQ(got.error().where->file.filename(), "10-x.conf");
}

TEST(PeerTest, PeerMissingPublicKeyIsAnError) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("10-x.conf", "[Peer]\nAllowedIPs = 10.8.2.2/32\n"));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_FALSE(got.has_value());
    EXPECT_EQ(got.error().code, wirebard::ErrorCode::config);
}

TEST(PeerTest, EmptyPeerStubIsIgnoredNotAnError) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("10-x.conf", "[Peer]\n# nothing here yet\n"));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_TRUE(got.has_value()) << wirebard::format_error(got.error());
    EXPECT_TRUE(got->empty());
}

TEST(PeerTest, BadAllowedIPsIsAnError) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("10-x.conf", "[Peer]\nPublicKey = AAA\nAllowedIPs = not-an-ip\n"));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_FALSE(got.has_value());
    EXPECT_EQ(got.error().code, wirebard::ErrorCode::config);
}

TEST(PeerTest, RenderPeerPartialRoundTripsThroughCollect) {
    std::string text = wirebard::render_peer_partial(
        "baki-web01", "PUBKEY=", wirebard::parse_ipv4("10.8.2.7").value());
    EXPECT_EQ(text, "# wirebard: name=baki-web01\n[Peer]\nPublicKey = PUBKEY=\n"
                    "AllowedIPs = 10.8.2.7/32\n");

    // What we render must parse back to the same assignment.
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("20-baki-web01.conf", text));
    auto got = wirebard::collect_assignments(parts);
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->size(), 1u);
    EXPECT_EQ((*got)[0].name, "baki-web01");
    EXPECT_EQ(wirebard::format_ipv4((*got)[0].address), "10.8.2.7");
}

TEST(PeerTest, NextPeerFilenameStepsPastHighestAndSanitizes) {
    namespace fs = std::filesystem;
    std::vector<fs::path> existing = {"00-main.conf", "10-alice.conf", "20-bob.conf"};
    EXPECT_EQ(wirebard::next_peer_filename(existing, "baki web/01"), "30-baki-web-01.conf");

    // Empty network → first peer at 10.
    EXPECT_EQ(wirebard::next_peer_filename({}, "solo"), "10-solo.conf");
    // Only the main partial present → still 10.
    std::vector<fs::path> only_main = {"00-main.conf"};
    EXPECT_EQ(wirebard::next_peer_filename(only_main, "x"), "10-x.conf");
    // All-unsafe name falls back to "peer".
    EXPECT_EQ(wirebard::next_peer_filename({}, "///"), "10-peer.conf");
}

} // namespace
