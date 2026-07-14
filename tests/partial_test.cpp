#include "partial.h"

#include <fstream>
#include <optional>

#include <gtest/gtest.h>

#include "fs.h"
#include "vars.h"

namespace {

class PartialTest : public ::testing::Test {
protected:
    void write(std::string_view name, std::string_view content) {
        std::ofstream out(dir_->path() / name);
        out << content;
    }

    void SetUp() override {
        auto d = wirebard::TempDir::create("partial-test");
        ASSERT_TRUE(d.has_value());
        dir_.emplace(std::move(*d));
    }

    std::optional<wirebard::TempDir> dir_;
};

TEST_F(PartialTest, LoadsInMergeOrderExcludingTemplate) {
    write("00-main.conf", "#= listen_port = 51820\n[Interface]\nListenPort = ${listen_port}\n");
    write("20-baki.conf", "[Peer]\nPublicKey = bbb\n");
    write("10-alice.conf", "[Peer]\nPublicKey = aaa\n");
    write("template.conf", "[Peer]\nPublicKey = TEMPLATE\n"); // must NOT be loaded
    write("server.key", "PRIVATEKEYBYTES");                   // not a .conf, ignored

    auto vars = wirebard::VarTable::load(dir_->path(), {});
    ASSERT_TRUE(vars.has_value()) << wirebard::format_error(vars.error());

    auto parts = wirebard::load_partials(dir_->path(), *vars);
    ASSERT_TRUE(parts.has_value()) << wirebard::format_error(parts.error());

    ASSERT_EQ(parts->size(), 3u); // 00, 10, 20 — template.conf and server.key excluded
    EXPECT_EQ((*parts)[0].path.filename(), "00-main.conf");
    EXPECT_EQ((*parts)[1].path.filename(), "10-alice.conf");
    EXPECT_EQ((*parts)[2].path.filename(), "20-baki.conf");

    // 00-main had its ${listen_port} substituted.
    EXPECT_NE((*parts)[0].raw.find("ListenPort = 51820"), std::string::npos);
    EXPECT_EQ((*parts)[0].raw.find("${"), std::string::npos);
}

TEST_F(PartialTest, EmptyNetworkIsAnError) {
    write("template.conf", "[Peer]\n"); // only the non-compiled reference
    auto vars = wirebard::VarTable::load(dir_->path(), {});
    ASSERT_FALSE(vars.has_value()); // vars load already refuses an empty network
    EXPECT_EQ(vars.error().code, wirebard::ErrorCode::io);
}

TEST_F(PartialTest, UndefinedVariableInAPeerPropagates) {
    write("00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    write("10-peer.conf", "[Peer]\nEndpoint = ${undeclared}\n");
    auto vars = wirebard::VarTable::load(dir_->path(), {});
    ASSERT_TRUE(vars.has_value());

    auto parts = wirebard::load_partials(dir_->path(), *vars);
    ASSERT_FALSE(parts.has_value());
    EXPECT_EQ(parts.error().code, wirebard::ErrorCode::undefined_var);
    EXPECT_EQ(parts.error().where->file.filename(), "10-peer.conf");
}

} // namespace
