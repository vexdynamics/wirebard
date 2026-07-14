#include "check.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "fs.h"
#include "project.h"

namespace fs = std::filesystem;

namespace {

class CheckTest : public ::testing::Test {
protected:
    void write(std::string_view name, std::string_view content) {
        fs::path dir = root() / "partials" / "net";
        fs::create_directories(dir);
        std::ofstream out(dir / name);
        out << content;
    }

    // Load the "net" network and check it; returns the diagnostics.
    std::vector<wirebard::Diagnostic> check() {
        auto paths = wirebard::ProjectPaths::locate(root());
        EXPECT_TRUE(paths.has_value());
        auto net = wirebard::Network::load(*paths, "net", {});
        EXPECT_TRUE(net.has_value()) << (net ? "" : wirebard::format_error(net.error()));
        return net ? wirebard::check_network(*net) : std::vector<wirebard::Diagnostic>{};
    }

    [[nodiscard]] fs::path root() const { return dir_->path(); }

    void SetUp() override {
        auto d = wirebard::TempDir::create("check-test");
        ASSERT_TRUE(d.has_value());
        dir_.emplace(std::move(*d));
    }

    std::optional<wirebard::TempDir> dir_;
};

constexpr std::string_view kGoodMain =
    "#= subnet = 10.8.2.0/24\n#= address = 10.8.2.1/24\n[Interface]\nAddress = ${address}\n";

TEST_F(CheckTest, CleanNetworkHasNoDiagnostics) {
    write("00-main.conf", kGoodMain);
    write("10-a.conf", "# wirebard: name=a\n[Peer]\nPublicKey = AAA\nAllowedIPs = 10.8.2.2/32\n");
    EXPECT_TRUE(check().empty());
}

TEST_F(CheckTest, FlagsDuplicateAddress) {
    write("00-main.conf", kGoodMain);
    write("10-a.conf", "[Peer]\nPublicKey = AAA\nAllowedIPs = 10.8.2.5/32\n");
    write("20-b.conf", "[Peer]\nPublicKey = BBB\nAllowedIPs = 10.8.2.5/32\n");
    auto d = check();
    ASSERT_FALSE(d.empty());
    EXPECT_EQ(d.front().error.code, wirebard::ErrorCode::config);
}

TEST_F(CheckTest, FlagsAddressOutsideSubnet) {
    write("00-main.conf", kGoodMain);
    write("10-a.conf", "[Peer]\nPublicKey = AAA\nAllowedIPs = 10.9.9.9/32\n");
    auto d = check();
    ASSERT_EQ(d.size(), 1u);
    EXPECT_NE(d.front().error.message.find("outside subnet"), std::string::npos);
}

TEST_F(CheckTest, FlagsCollisionWithServerAddress) {
    write("00-main.conf", kGoodMain);
    write("10-a.conf", "[Peer]\nPublicKey = AAA\nAllowedIPs = 10.8.2.1/32\n"); // == server
    auto d = check();
    ASSERT_EQ(d.size(), 1u);
    EXPECT_NE(d.front().error.message.find("server address"), std::string::npos);
}

TEST_F(CheckTest, FlagsMissingSubnetVariable) {
    write("00-main.conf", "#= address = 10.8.2.1/24\n[Interface]\nAddress = ${address}\n");
    write("10-a.conf", "[Peer]\nPublicKey = AAA\nAllowedIPs = 10.8.2.2/32\n");
    auto d = check();
    ASSERT_FALSE(d.empty());
    EXPECT_NE(d.front().error.message.find("subnet"), std::string::npos);
}

} // namespace
