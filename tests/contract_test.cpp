#include "contract.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

wirebard::PeerResult split_result() {
    return wirebard::PeerResult{.network = "backups",
                                .full_tunnel = false,
                                .address = "10.8.2.5/24",
                                .subnet = "10.8.2.0/24",
                                .server_public_key = "SRVPUB=",
                                .endpoint = "vpn.example.com:51820",
                                .dns = "10.8.2.1",
                                .mtu = "1420"};
}

TEST(ContractTest, SplitTunnelClientConfigRoutesOnlySubnet) {
    std::string cfg = wirebard::render_client_config(split_result());
    const std::string expected = "[Interface]\n"
                                 "PrivateKey = {{PRIVATE_KEY}}\n"
                                 "Address = 10.8.2.5/24\n"
                                 "DNS = 10.8.2.1\n"
                                 "MTU = 1420\n"
                                 "\n"
                                 "[Peer]\n"
                                 "PublicKey = SRVPUB=\n"
                                 "Endpoint = vpn.example.com:51820\n"
                                 "AllowedIPs = 10.8.2.0/24\n";
    EXPECT_EQ(cfg, expected);
}

TEST(ContractTest, FullTunnelRoutesEverythingAndOmitsEmptyFields) {
    wirebard::PeerResult r = split_result();
    r.full_tunnel = true;
    r.dns = "";
    r.mtu = "";
    std::string cfg = wirebard::render_client_config(r);
    EXPECT_NE(cfg.find("AllowedIPs = 0.0.0.0/0, ::/0\n"), std::string::npos);
    EXPECT_EQ(cfg.find("DNS ="), std::string::npos); // omitted
    EXPECT_EQ(cfg.find("MTU ="), std::string::npos);
}

TEST(ContractTest, PeerAddEnvelopeMatchesTheContract) {
    std::string json = wirebard::peer_add_json(split_result());
    // Field order and names per docs §7; client_config's newlines are escaped.
    const std::string expected =
        R"({"network":"backups","type":"isolated","address":"10.8.2.5/24",)"
        R"("server_public_key":"SRVPUB=","endpoint":"vpn.example.com:51820",)"
        R"("client_config":"[Interface]\nPrivateKey = {{PRIVATE_KEY}}\n)"
        R"(Address = 10.8.2.5/24\nDNS = 10.8.2.1\nMTU = 1420\n\n[Peer]\n)"
        R"(PublicKey = SRVPUB=\nEndpoint = vpn.example.com:51820\n)"
        R"(AllowedIPs = 10.8.2.0/24\n"})";
    EXPECT_EQ(json, expected);
}

TEST(ContractTest, FullTunnelEnvelopeTypeIsProxy) {
    wirebard::PeerResult r = split_result();
    r.full_tunnel = true;
    EXPECT_NE(wirebard::peer_add_json(r).find(R"("type":"proxy")"), std::string::npos);
}

TEST(ContractTest, NetworkListEnvelope) {
    std::vector<wirebard::NetworkSummary> nets = {
        {.name = "backups", .full_tunnel = false, .subnet = "10.8.2.0/24"},
        {.name = "roam", .full_tunnel = true, .subnet = "10.9.0.0/24"},
    };
    EXPECT_EQ(wirebard::network_list_json(nets),
              R"([{"name":"backups","type":"isolated","subnet":"10.8.2.0/24"},)"
              R"({"name":"roam","type":"proxy","subnet":"10.9.0.0/24"}])");
    EXPECT_EQ(wirebard::network_list_json({}), "[]");
}

} // namespace
