#include "render.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "partial.h"

namespace {

wirebard::Partial part(std::string path, std::string raw) {
    return wirebard::Partial{.path = std::move(path), .raw = std::move(raw)};
}

std::vector<wirebard::Partial> sample_partials() {
    std::vector<wirebard::Partial> parts;
    parts.push_back(
        part("00-main.conf", "[Interface]\nAddress = 10.8.2.1/24\nListenPort = 51820\n"));
    parts.push_back(part("10-alice.conf", "# wirebard: name=alice\n[Peer]\n"
                                          "PublicKey = AAA\nAllowedIPs = 10.8.2.2/32\n"));
    return parts;
}

TEST(RenderTest, ByteIdenticalWithInjectedPrivateKey) {
    auto parts = sample_partials();
    std::string out = wirebard::compile(
        parts, {.interface = "backups", .env_label = "", .server_private_key = "SVRKEY"});

    const std::string expected =
        "# Managed by wirebard — DO NOT EDIT.\n"
        "# Compiled from partials/backups/ — edit those, then `wirebard build`.\n"
        "\n"
        "# ===== 00-main.conf =====\n"
        "[Interface]\n"
        "PrivateKey = SVRKEY\n" // injected right under [Interface]
        "Address = 10.8.2.1/24\n"
        "ListenPort = 51820\n"
        "\n"
        "# ===== 10-alice.conf =====\n"
        "# wirebard: name=alice\n"
        "[Peer]\n"
        "PublicKey = AAA\n"
        "AllowedIPs = 10.8.2.2/32\n"
        "\n";
    EXPECT_EQ(out, expected);
}

TEST(RenderTest, OmitsPrivateKeyWhenNoneGivenAndAddsEnvBanner) {
    auto parts = sample_partials();
    std::string out = wirebard::compile(
        parts, {.interface = "backups", .env_label = "prod", .server_private_key = ""});

    EXPECT_EQ(out.find("PrivateKey"), std::string::npos); // no key line
    EXPECT_NE(out.find("# Environment: prod\n"), std::string::npos);
}

TEST(RenderTest, IsDeterministic) {
    auto parts = sample_partials();
    wirebard::CompileOptions opts{
        .interface = "backups", .env_label = "", .server_private_key = "K"};
    EXPECT_EQ(wirebard::compile(parts, opts), wirebard::compile(parts, opts));
}

TEST(RenderTest, NormalizesMissingTrailingNewline) {
    std::vector<wirebard::Partial> parts;
    parts.push_back(part("00-main.conf", "[Interface]")); // no trailing newline
    std::string out =
        wirebard::compile(parts, {.interface = "x", .env_label = "", .server_private_key = ""});
    EXPECT_NE(out.find("[Interface]\n"), std::string::npos); // got its newline
}

} // namespace
