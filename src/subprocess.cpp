#include "subprocess.h"

#include <cerrno>
#include <cstring>
#include <format>
#include <print>
#include <utility>
#include <vector>

#include <array>
#include <csignal>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace wirebard {

namespace {

// C++ LESSON: the flagship RAII example — one file descriptor, owned.
// An int fd is the easiest resource in the world to leak (early return,
// exception, forgotten branch) or to close twice. Wrap it once, and the
// compiler runs close() on every exit path for you. This 20-line pattern is
// how unique_ptr, ifstream, lock_guard and every other std RAII type works
// inside; after this file you can write one for any C resource (mutexes,
// sockets, mmaps, lock files...).
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    UniqueFd(const UniqueFd&) = delete; // copying = double close
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    ~UniqueFd() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

Error sys_error(std::string_view what) {
    return Error{.code = ErrorCode::subprocess,
                 .message = std::format("{}: {}", what, std::strerror(errno))};
}

bool g_verbose = false; // written once at startup, read-only afterwards

} // namespace

void set_verbose(bool on) noexcept { g_verbose = on; }

Result<CommandResult> run_command(std::span<const std::string> argv, std::chrono::seconds timeout) {
    if (argv.empty()) {
        return std::unexpected(Error{.code = ErrorCode::subprocess, .message = "empty argv"});
    }

    if (g_verbose) {
        std::string echo = "+";
        for (const std::string& a : argv) {
            echo += ' ';
            echo += a;
        }
        std::println(stderr, "{}", echo);
    }

    // One pipe per stream. pipe() fills [read_end, write_end]. std::array at
    // a C boundary: .data() hands the C API the raw int[2] it wants, and we
    // keep a real container type on our side.
    std::array<int, 2> out_pipe{};
    std::array<int, 2> err_pipe{};
    if (::pipe(out_pipe.data()) != 0)
        return std::unexpected(sys_error("pipe(stdout)"));
    UniqueFd out_r(out_pipe[0]);
    UniqueFd out_w(out_pipe[1]);
    if (::pipe(err_pipe.data()) != 0)
        return std::unexpected(sys_error("pipe(stderr)"));
    UniqueFd err_r(err_pipe[0]);
    UniqueFd err_w(err_pipe[1]);
    // Those four UniqueFds mean: no matter which return below fires, no fd
    // leaks. Without RAII this function would need close() calls sprinkled
    // through every error branch — and one would be missing.

    // execvp wants char* const[] (a C API from 1979 predating const).
    // The const_cast is safe: execvp never writes through these pointers.
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const std::string& a : argv) {
        cargv.push_back(const_cast<char*>(a.c_str()));
    }
    cargv.push_back(nullptr); // execvp's argv is null-terminated

    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(sys_error("fork"));

    if (pid == 0) {
        // === CHILD === Between fork and exec only async-signal-safe calls
        // are allowed (no malloc, no printf — the heap may be mid-mutation
        // in the parent snapshot we inherited).
        ::dup2(out_w.get(), STDOUT_FILENO);
        ::dup2(err_w.get(), STDERR_FILENO);
        // Close ALL pipe fds: the duplicated 1/2 suffice. A surviving write
        // end would keep the parent's poll() from ever seeing EOF.
        out_r.reset();
        out_w.reset();
        err_r.reset();
        err_w.reset();
        ::execvp(cargv[0], cargv.data());
        // Only reached if exec failed. _exit (not exit): no destructors, no
        // atexit handlers — this process is a doomed clone, leave quietly.
        ::_exit(127);
    }

    // === PARENT === Close our copies of the write ends NOW. The child holds
    // the only ones left; when it exits, reads return EOF. (Forgetting this
    // is the classic "my pipe never closes" deadlock.)
    out_w.reset();
    err_w.reset();

    CommandResult result;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    struct Stream {
        UniqueFd* fd;
        std::string* into;
    };
    std::array<Stream, 2> streams{Stream{.fd = &out_r, .into = &result.out},
                                  Stream{.fd = &err_r, .into = &result.err}};

    // Read both pipes until EOF or deadline. poll() sleeps until data
    // arrives or the timeout slice elapses — no busy-waiting.
    bool timed_out = false;
    while (streams[0].fd->valid() || streams[1].fd->valid()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            timed_out = true;
            ::kill(pid, SIGKILL);
            break;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

        std::array<pollfd, 2> fds{};
        nfds_t nfds = 0;
        for (const Stream& s : streams) {
            if (s.fd->valid()) {
                fds[nfds++] = pollfd{.fd = s.fd->get(), .events = POLLIN, .revents = 0};
            }
        }
        const int rc = ::poll(fds.data(), nfds, static_cast<int>(remaining.count()));
        if (rc < 0) {
            if (errno == EINTR)
                continue; // interrupted by a signal: retry
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            return std::unexpected(sys_error("poll"));
        }
        if (rc == 0)
            continue; // timeout slice expired; loop re-checks deadline

        for (nfds_t i = 0; i < nfds; ++i) {
            if (fds[i].revents == 0)
                continue;
            for (Stream& s : streams) {
                if (s.fd->valid() && s.fd->get() == fds[i].fd) {
                    std::array<char, 4096> buf; // NOLINT(*-member-init) — read() fills it
                    const ssize_t n = ::read(fds[i].fd, buf.data(), buf.size());
                    if (n > 0) {
                        s.into->append(buf.data(), static_cast<size_t>(n));
                    } else {
                        s.fd->reset(); // EOF (or error): stop watching
                    }
                }
            }
        }
    }

    // Reap the child — skipping waitpid leaks a zombie process entry.
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return std::unexpected(sys_error("waitpid"));
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = -1; // signal death; timeout below is the one we cause
    }

    if (timed_out) {
        return std::unexpected(
            Error{.code = ErrorCode::subprocess,
                  .message = std::format("'{}' timed out after {}s and was killed", argv[0],
                                         timeout.count())});
    }
    return result;
}

} // namespace wirebard
