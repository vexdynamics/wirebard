// Exercises fork/exec/pipes/timeout against coreutils — deterministic, no
// external tools needed, and ASan watches every fd and byte along the way.
#include "subprocess.h"

#include <chrono>

#include <gtest/gtest.h>

namespace {

using wirebard::run_command;
using namespace std::chrono_literals;

std::vector<std::string> argv(std::initializer_list<const char*> parts) {
    return {parts.begin(), parts.end()};
}

TEST(Subprocess, CapturesStdout) {
    auto r = run_command(argv({"echo", "hello", "world"}));
    ASSERT_TRUE(r.has_value()) << wirebard::format_error(r.error());
    EXPECT_EQ(r->exit_code, 0);
    EXPECT_TRUE(r->ok());
    EXPECT_EQ(r->out, "hello world\n");
    EXPECT_EQ(r->err, "");
}

TEST(Subprocess, SeparatesStderrAndExitCode) {
    // /bin/sh used as a test FIXTURE with a fixed script — the API itself
    // never invokes a shell on our behalf.
    auto r = run_command(argv({"/bin/sh", "-c", "echo to-out; echo to-err 1>&2; exit 3"}));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 3);
    EXPECT_FALSE(r->ok());
    EXPECT_EQ(r->out, "to-out\n");
    EXPECT_EQ(r->err, "to-err\n");
}

TEST(Subprocess, NonZeroExitIsNotAnError) {
    // "ran and said no" must be a success value, not an Error.
    auto r = run_command(argv({"false"}));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 1);
}

TEST(Subprocess, MissingBinaryYields127) {
    auto r = run_command(argv({"definitely-not-a-real-binary-xyzzy"}));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 127); // exec failed in the child
}

TEST(Subprocess, TimeoutKillsTheChild) {
    const auto start = std::chrono::steady_clock::now();
    auto r = run_command(argv({"sleep", "30"}), 1s);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(r.has_value()); // timeout IS an Error (couldn't get an answer)
    EXPECT_EQ(r.error().code, wirebard::ErrorCode::subprocess);
    // Killed promptly — nowhere near the 30s the child wanted.
    EXPECT_LT(elapsed, 5s);
}

TEST(Subprocess, LargeOutputDoesNotDeadlock) {
    // Classic pipe bug: a child writing more than the ~64KB pipe buffer
    // blocks forever if the parent isn't draining. Prove we drain.
    auto r = run_command(argv({"/bin/sh", "-c", "head -c 1000000 /dev/zero | tr '\\0' 'x'"}));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->out.size(), 1000000u);
}

TEST(Subprocess, EmptyArgvRejected) {
    auto r = run_command({});
    ASSERT_FALSE(r.has_value());
}

} // namespace
