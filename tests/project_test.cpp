#include "project.h"

#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

#include "fs.h"

namespace fs = std::filesystem;

namespace {

// Builds a whole project tree under a temp root:
//   <root>/partials/<network>/<file> ...
class ProjectTest : public ::testing::Test {
protected:
    // Write <root>/partials/<network>/<name> with content, creating dirs.
    void write(std::string_view network, std::string_view name, std::string_view content) {
        fs::path dir = root() / "partials" / network;
        fs::create_directories(dir);
        std::ofstream out(dir / name);
        out << content;
    }

    [[nodiscard]] fs::path root() const { return dir_->path(); }

    void SetUp() override {
        auto d = wirebard::TempDir::create("project-test");
        ASSERT_TRUE(d.has_value());
        dir_.emplace(std::move(*d));
    }

    std::optional<wirebard::TempDir> dir_;
};

TEST_F(ProjectTest, ResolveExplicitDirWins) {
    auto r = wirebard::resolve_project_root(fs::path("/some/where"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, fs::path("/some/where")); // trusted verbatim; locate() validates later
}

TEST_F(ProjectTest, LocateRequiresPartialsDir) {
    // Root exists but has no partials/ — locate must refuse with guidance.
    auto bad = wirebard::ProjectPaths::locate(root());
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, wirebard::ErrorCode::io);

    write("backups", "00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    auto ok = wirebard::ProjectPaths::locate(root());
    ASSERT_TRUE(ok.has_value()) << wirebard::format_error(ok.error());
    EXPECT_EQ(ok->partials_dir, root() / "partials");
}

TEST_F(ProjectTest, ListNetworksSortedAndSkipsEmptyFolders) {
    write("roam", "00-main.conf", "#= listen_port = 51821\n[Interface]\n");
    write("backups", "00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    write("scaffold", "template.conf", "[Peer]\n"); // no compiled partial → skipped

    auto paths = wirebard::ProjectPaths::locate(root());
    ASSERT_TRUE(paths.has_value());
    auto nets = wirebard::list_networks(*paths);
    ASSERT_TRUE(nets.has_value()) << wirebard::format_error(nets.error());

    ASSERT_EQ(nets->size(), 2u);
    EXPECT_EQ((*nets)[0].name, "backups"); // sorted by name
    EXPECT_EQ((*nets)[1].name, "roam");
    EXPECT_EQ((*nets)[0].output, root() / "backups.conf");
    EXPECT_EQ((*nets)[0].server_key, root() / "partials" / "backups" / "server.key");
}

TEST_F(ProjectTest, NetworkLoadHappyPath) {
    write("backups", "00-main.conf",
          "#= subnet = 10.8.2.0/24\n#= listen_port = 51820\n"
          "[Interface]\nListenPort = ${listen_port}\n");
    write("backups", "10-alice.conf", "[Peer]\nPublicKey = aaa\nAllowedIPs = 10.8.2.2/32\n");

    auto paths = wirebard::ProjectPaths::locate(root());
    ASSERT_TRUE(paths.has_value());

    auto net = wirebard::Network::load(*paths, "backups", {});
    ASSERT_TRUE(net.has_value()) << wirebard::format_error(net.error());
    EXPECT_EQ(net->paths.name, "backups");
    EXPECT_EQ(net->vars.get("subnet"), "10.8.2.0/24");
    ASSERT_EQ(net->partials.size(), 2u);
    EXPECT_NE(net->partials[0].raw.find("ListenPort = 51820"), std::string::npos);
    EXPECT_NE(net->partials[1].raw.find("10.8.2.2/32"), std::string::npos);
}

TEST_F(ProjectTest, NetworkLoadUnknownNameErrors) {
    write("backups", "00-main.conf", "#= listen_port = 51820\n[Interface]\n");
    auto paths = wirebard::ProjectPaths::locate(root());
    ASSERT_TRUE(paths.has_value());

    auto net = wirebard::Network::load(*paths, "does-not-exist", {});
    ASSERT_FALSE(net.has_value());
    EXPECT_EQ(net.error().code, wirebard::ErrorCode::io);
}

} // namespace
