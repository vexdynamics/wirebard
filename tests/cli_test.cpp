// Tests for the argv parser. GoogleTest 101 for a jest/go-test person:
// TEST(Suite, Name) declares a test; EXPECT_* records a failure and keeps
// going (like t.Errorf), ASSERT_* aborts the test on failure (like t.Fatalf —
// use it when continuing would just crash or spam).
#include "cli.h"

#include <gtest/gtest.h>

namespace {

// Helper: build argv the way the OS delivers it (array of C strings).
wirebard::Result<wirebard::ParsedArgs> parse(std::initializer_list<const char*> args) {
    std::vector<const char*> argv{"wirebard"};
    argv.insert(argv.end(), args);
    return wirebard::parse_args(std::span(argv.data(), argv.size()));
}

TEST(Cli, ParsesCommandWithFlagsAndSwitches) {
    auto r = parse({"-C", "example", "build", "--env", "prod", "--verbose"});
    ASSERT_TRUE(r.has_value()) << wirebard::format_error(r.error());
    EXPECT_EQ(r->command, "build");
    ASSERT_TRUE(r->dir.has_value());
    EXPECT_EQ(*r->dir, "example");
    EXPECT_EQ(r->flags.at("--env"), "prod");
    EXPECT_TRUE(r->switches.contains("--verbose"));
}

TEST(Cli, CollectsPositionals) {
    auto r = parse({"list", "wg0", "wg1"});
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->positionals.size(), 2u);
    EXPECT_EQ(r->positionals[0], "wg0");
    EXPECT_EQ(r->positionals[1], "wg1");
}

TEST(Cli, NoCommandIsUsageError) {
    auto r = parse({});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::usage);
}

TEST(Cli, UnknownCommandIsUsageError) {
    auto r = parse({"frobnicate"});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::usage);
}

TEST(Cli, UnknownFlagIsUsageError) {
    auto r = parse({"build", "--frobnicate"});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::usage);
}

TEST(Cli, ValueFlagWithoutValueIsUsageError) {
    auto r = parse({"build", "--env"});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::usage);
}

TEST(Cli, HelpNeedsNoCommand) {
    auto r = parse({"--help"});
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->switches.contains("--help"));
}

TEST(Cli, DirIsUnsetWithoutFlag) {
    auto r = parse({"build"});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->dir, std::nullopt);
}

} // namespace
