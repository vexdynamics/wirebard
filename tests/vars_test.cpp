#include "vars.h"

#include <fstream>
#include <optional>

#include <gtest/gtest.h>

#include "fs.h"

namespace {

// A throwaway network folder per test. GoogleTest builds a fresh fixture for
// every TEST_F, so tests can't contaminate each other; TempDir's destructor
// cleans up even when a test fails.
class VarsTest : public ::testing::Test {
protected:
    void write(std::string_view name, std::string_view content) {
        std::ofstream out(dir_->path() / name);
        out << content;
    }

    wirebard::Result<wirebard::VarTable> load(std::optional<std::string_view> env = {}) {
        return wirebard::VarTable::load(dir_->path(), env);
    }

    void SetUp() override {
        auto d = wirebard::TempDir::create("vars-test");
        ASSERT_TRUE(d.has_value());
        dir_.emplace(std::move(*d));
    }

    std::optional<wirebard::TempDir> dir_;
};

TEST_F(VarsTest, LoadsDirectiveLines) {
    write("00-main.conf", R"(# the main partial carries its values inline
#= subnet = 10.8.2.0/24
#= listen_port = 51820
#= endpoint = vpn.example.com:51820
[Interface]
ListenPort = ${listen_port}
)");
    auto t = load();
    ASSERT_TRUE(t.has_value()) << wirebard::format_error(t.error());
    EXPECT_EQ(t->get("subnet"), "10.8.2.0/24");
    EXPECT_EQ(t->get("listen_port"), "51820");
    EXPECT_EQ(t->get("endpoint"), "vpn.example.com:51820"); // ':' in value is fine
    EXPECT_EQ(t->get("missing"), std::nullopt);
}

TEST_F(VarsTest, VariablesOutsideTheMainPartialAreAnError) {
    // Variables are defined ONCE, in the first partial — a stray '#=' in a
    // later partial must fail loudly, pointing at the offending file.
    write("00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    write("20-peer.conf", "[Peer]\nPublicKey = abc\n#= listen_port = 9999\n");
    auto t = load();
    ASSERT_FALSE(t.has_value());
    EXPECT_EQ(t.error().code, wirebard::ErrorCode::directive_parse);
    ASSERT_TRUE(t.error().where.has_value());
    EXPECT_EQ(t.error().where->file.filename(), "20-peer.conf");
    EXPECT_EQ(t.error().where->line, 3);
}

TEST_F(VarsTest, TemplateConfIsIgnoredForVariables) {
    // template.conf is a .conf but never compiled — a #= inside it is neither
    // a definition nor a "stray directive" error; it is simply invisible.
    write("00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    write("template.conf", "#= sneaky = 1\n[Peer]\nPublicKey = REPLACE\n");
    auto t = load();
    ASSERT_TRUE(t.has_value()) << wirebard::format_error(t.error());
    EXPECT_EQ(t->get("sneaky"), std::nullopt);
    EXPECT_EQ(t->get("listen_port"), "51820");
}

TEST_F(VarsTest, EnvOverlayWinsRegardlessOfOrder) {
    write("00-main.conf", R"(#= endpoint = vpn.example.com:51820
#= prod: endpoint = vpn.prod.example.com:51820
#= listen_port = 51820
)");
    auto base = load();
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(base->get("endpoint"), "vpn.example.com:51820");

    auto prod = load("prod");
    ASSERT_TRUE(prod.has_value()) << wirebard::format_error(prod.error());
    EXPECT_EQ(prod->get("endpoint"), "vpn.prod.example.com:51820");
    EXPECT_EQ(prod->get("listen_port"), "51820"); // untouched base value
}

TEST_F(VarsTest, UnknownEnvIsAnError) {
    write("00-main.conf", "#= listen_port = 51820\n#= prod: listen_port = 51821\n");
    auto t = load("staging"); // no #= staging: line exists
    ASSERT_FALSE(t.has_value());
    EXPECT_EQ(t.error().code, wirebard::ErrorCode::io);
}

TEST_F(VarsTest, EmptyNetworkIsAnError) {
    auto t = load(); // no *.conf at all
    ASSERT_FALSE(t.has_value());
    EXPECT_EQ(t.error().code, wirebard::ErrorCode::io);
}

TEST_F(VarsTest, SubstituteReplacesAndEscapes) {
    write("00-main.conf", "#= address = 10.8.2.1/24\n");
    auto t = load();
    ASSERT_TRUE(t.has_value());

    auto ok = wirebard::substitute("Address = ${address}", *t, "x.conf");
    ASSERT_TRUE(ok.has_value()) << wirebard::format_error(ok.error());
    EXPECT_EQ(*ok, "Address = 10.8.2.1/24");

    // $${x} renders as a literal ${x}; a lone '%i' (WireGuard syntax) survives.
    auto esc = wirebard::substitute("$${address} PostUp %i up", *t, "x.conf");
    ASSERT_TRUE(esc.has_value());
    EXPECT_EQ(*esc, "${address} PostUp %i up");
}

TEST_F(VarsTest, SubstituteUndefinedVariableFailsWithLocation) {
    write("00-main.conf", "#= address = 10.8.2.1/24\n");
    auto t = load();
    ASSERT_TRUE(t.has_value());

    auto bad = wirebard::substitute("line one\nEndpoint = ${nope}\n", *t, "10-peer.conf");
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, wirebard::ErrorCode::undefined_var);
    ASSERT_TRUE(bad.error().where.has_value());
    EXPECT_EQ(bad.error().where->line, 2);
}

} // namespace
