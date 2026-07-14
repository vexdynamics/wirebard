#include "fs.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

TEST(Fs, ReadFileRoundTrip) {
    auto dir = wirebard::TempDir::create("fs-test");
    ASSERT_TRUE(dir.has_value());
    fs::path p = dir->path() / "hello.txt";
    {
        std::ofstream out(p);
        out << "line one\nline two\n";
    }
    auto content = wirebard::read_file(p);
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "line one\nline two\n");
}

TEST(Fs, ReadMissingFileIsIoError) {
    auto content = wirebard::read_file("/nonexistent/definitely/not/here.txt");
    ASSERT_FALSE(content.has_value());
    EXPECT_EQ(content.error().code, wirebard::ErrorCode::io);
    ASSERT_TRUE(content.error().where.has_value()); // diagnostic carries the path
}

TEST(Fs, AtomicWriteCreatesFileWithExactContents) {
    auto dir = wirebard::TempDir::create("fs-test");
    ASSERT_TRUE(dir.has_value());
    fs::path p = dir->path() / "out.cfg";

    auto res = wirebard::atomic_write_file(p, "generated config\n");
    ASSERT_TRUE(res.has_value());

    auto back = wirebard::read_file(p);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, "generated config\n");
}

TEST(Fs, AtomicWriteOverwritesAndLeavesNoTempDroppings) {
    auto dir = wirebard::TempDir::create("fs-test");
    ASSERT_TRUE(dir.has_value());
    fs::path p = dir->path() / "out.cfg";

    ASSERT_TRUE(wirebard::atomic_write_file(p, "v1").has_value());
    ASSERT_TRUE(wirebard::atomic_write_file(p, "v2").has_value());

    EXPECT_EQ(*wirebard::read_file(p), "v2");
    // The whole point of temp+rename: nothing but the target remains.
    size_t entries = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir->path())) {
        ++entries;
    }
    EXPECT_EQ(entries, 1u);
}

TEST(Fs, AtomicWriteToBadDirectoryFailsCleanly) {
    auto res = wirebard::atomic_write_file("/nonexistent/dir/out.cfg", "x");
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, wirebard::ErrorCode::io);
}

TEST(Fs, TempDirRemovesItselfOnDestruction) {
    fs::path remembered;
    {
        auto dir = wirebard::TempDir::create("fs-test");
        ASSERT_TRUE(dir.has_value());
        remembered = dir->path();
        EXPECT_TRUE(fs::exists(remembered));
        std::ofstream(remembered / "junk.txt") << "junk";
    } // destructor fires here
    EXPECT_FALSE(fs::exists(remembered));
}

TEST(Fs, TempDirMoveTransfersOwnership) {
    auto a = wirebard::TempDir::create("fs-test");
    ASSERT_TRUE(a.has_value());
    fs::path p = a->path();

    wirebard::TempDir b = std::move(*a); // b now owns the directory
    EXPECT_EQ(b.path(), p);
    EXPECT_TRUE(fs::exists(p));
    // ASan would flag a double-delete here if move semantics were wrong;
    // both destructors run at scope end, but only b's does real work.
}

} // namespace
