#include "fs.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

// POSIX headers for fsync — C++ has no portable fsync, and a config tool for
// a live daemon genuinely needs it. Mixing C APIs into C++ is normal and fine.
#include <fcntl.h>
#include <sys/file.h> // flock
#include <unistd.h>

namespace wirebard {

namespace fs = std::filesystem; // namespace alias, like `import fs from ...`

// C++ LESSON: an anonymous namespace makes these helpers PRIVATE to this .cpp
// file (internal linkage) — the equivalent of NOT exporting a function in a
// JS module, or a lowercase name in Go. Without it, two .cpp files defining
// helpers with the same name would collide at link time (ODR violation).
namespace {

Error io_error(const fs::path& p, std::string_view what) {
    // errno is the C world's thread-local "last OS error"; strerror turns it
    // into text. Capture it BEFORE doing anything else that might reset it.
    return Error{.code = ErrorCode::io,
                 .message = std::format("{}: {}", what, std::strerror(errno)),
                 .where = SourceLoc{.file = p}};
}

// Random suffix for temp names. Not cryptographic; collision just fails politely.
std::string random_suffix() {
    // C++ LESSON: `static` inside a function = initialized once, on first
    // call, thread-safely — the sanctioned lazy singleton. random_device
    // seeds from the OS; mt19937_64 is the workhorse PRNG.
    static std::mt19937_64 rng{std::random_device{}()};
    return std::format("{:016x}", rng());
}

} // namespace

Result<std::vector<fs::path>> list_files(const fs::path& dir, std::string_view suffix) {
    if (!fs::is_directory(dir)) {
        return std::unexpected(Error{.code = ErrorCode::io,
                                     .message = "directory not found",
                                     .where = SourceLoc{.file = dir}});
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        // extension() only sees the LAST dot-part; a compound suffix like
        // ".peer.conf" needs a plain ends_with on the whole filename.
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(suffix)) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files, {}, [](const fs::path& p) { return p.filename(); });
    // C++ LESSON: that third argument is a "projection" — sort by KEY(x)
    // instead of x. Go's sort.Slice comparator and JS's .sort(fn) bake the
    // key into the comparison; ranges lets you say "compare filenames" once.
    return files;
}

Result<std::string> read_file(const fs::path& path) {
    // C++ LESSON: std::ifstream is itself RAII — the file handle closes in
    // its destructor no matter how we leave this function. That's why you
    // never see `defer f.Close()` in C++: the type does it.
    // `binary` because we want bytes as-is, no newline translation.
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(Error{.code = ErrorCode::io,
                                     .message = "cannot open file for reading",
                                     .where = SourceLoc{.file = path}});
    }
    std::ostringstream buf;
    buf << in.rdbuf(); // slurp the whole stream
    if (in.bad()) {    // bad() = actual I/O error (vs eof, which is normal)
        return std::unexpected(io_error(path, "read failed"));
    }
    // C++ LESSON: std::move — buf.str() returns a fresh string; moving it
    // into the Result transfers the heap buffer instead of copying it.
    // A move is a pointer swap; a copy is a full allocation + memcpy. The
    // compiler often elides even this, but writing intent explicitly is free.
    return std::move(buf).str();
}

namespace {

// The shared temp+fsync+rename dance. `mode`, when set, is applied to the temp
// file BEFORE the rename, so the destination is never briefly world-readable.
Result<void> write_atomic(const fs::path& dest, std::string_view contents,
                          std::optional<fs::perms> mode) {
    // Temp file must be in the SAME directory as dest: rename(2) is only
    // atomic within one filesystem, and /tmp is often a different one.
    fs::path tmp = dest;
    tmp += ".tmp." + random_suffix();

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected(io_error(tmp, "cannot open temp file for writing"));
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            // Cleanup on the error path. `ec` variants don't throw; we're
            // already failing, a secondary failure here is not interesting.
            std::error_code ec;
            fs::remove(tmp, ec);
            return std::unexpected(io_error(tmp, "write failed"));
        }
    } // <- ofstream destructor closes the OS handle here; fsync needs it closed-or-flushed

    // Tighten permissions on the temp file before it becomes visible under the
    // real name. `replace` sets the bits exactly (not add/remove).
    if (mode) {
        std::error_code ec;
        fs::permissions(tmp, *mode, fs::perm_options::replace, ec);
        if (ec) {
            std::error_code ec2;
            fs::remove(tmp, ec2);
            return std::unexpected(Error{.code = ErrorCode::io,
                                         .message = std::format("chmod failed: {}", ec.message()),
                                         .where = SourceLoc{.file = dest}});
        }
    }

    // fsync: force the bytes to disk. Without this, a power cut after rename
    // can leave an EMPTY renamed file (the rename metadata hit disk before
    // the data did). Belt and braces for a daemon's config.
    {
        int fd = ::open(tmp.c_str(), O_RDONLY); // `::` = global (C) namespace, not wirebard::open
        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
    }

    std::error_code ec;
    fs::rename(tmp, dest, ec); // the atomic moment
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return std::unexpected(Error{.code = ErrorCode::io,
                                     .message = std::format("rename failed: {}", ec.message()),
                                     .where = SourceLoc{.file = dest}});
    }
    // C++ LESSON: Result<void> still needs an explicit "success" — an empty
    // braced value. (expected<void, E> has a void success slot.)
    return {};
}

} // namespace

Result<void> atomic_write_file(const fs::path& dest, std::string_view contents) {
    return write_atomic(dest, contents, std::nullopt);
}

Result<void> atomic_write_file(const fs::path& dest, std::string_view contents, fs::perms mode) {
    return write_atomic(dest, contents, mode);
}

Result<FileLock> FileLock::acquire(const fs::path& lockfile, std::chrono::milliseconds timeout) {
    // O_CREAT so the lockfile springs into existence; 0600 because it lives
    // beside secret-bearing configs. We hold the fd, not the file's contents.
    int fd = ::open(lockfile.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return std::unexpected(io_error(lockfile, "cannot open lock file"));
    }

    // Non-blocking attempts on a fixed cadence until the deadline. flock ties
    // the lock to THIS open file description, so a second acquire() — even in
    // the same process — blocks, which is exactly what serializes callers.
    constexpr auto step = std::chrono::milliseconds{50};
    auto waited = std::chrono::milliseconds{0};
    for (;;) {
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
            return FileLock(fd);
        }
        if (errno != EWOULDBLOCK) {
            Error e = io_error(lockfile, "flock failed");
            ::close(fd);
            return std::unexpected(std::move(e));
        }
        if (waited >= timeout) {
            ::close(fd);
            return std::unexpected(
                Error{.code = ErrorCode::io,
                      .message = "another wirebard is holding this network's lock",
                      .where = SourceLoc{.file = lockfile}});
        }
        std::this_thread::sleep_for(step);
        waited += step;
    }
}

FileLock::FileLock(FileLock&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

FileLock::~FileLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN); // closing would release it anyway; explicit is clearer
        ::close(fd_);
    }
}

Result<TempDir> TempDir::create(std::string_view prefix) {
    fs::path p = fs::temp_directory_path() / std::format("wirebard-{}-{}", prefix, random_suffix());
    std::error_code ec;
    if (!fs::create_directory(p, ec) || ec) {
        return std::unexpected(Error{
            .code = ErrorCode::io,
            .message = std::format("cannot create temp dir: {}", ec ? ec.message() : "exists"),
            .where = SourceLoc{.file = p}});
    }
    return TempDir(std::move(p));
}

// Move constructor: steal the path, leave the source EMPTY so its destructor
// becomes a no-op. std::exchange does "take the old value, plant a new one"
// in a single expression — the idiomatic move-implementation tool.
TempDir::TempDir(TempDir&& other) noexcept : path_(std::exchange(other.path_, {})) {}

TempDir& TempDir::operator=(TempDir&& other) noexcept {
    if (this != &other) {
        // Assignment overwrites a live TempDir: release OUR directory first,
        // then steal theirs. Miss that and the old directory leaks forever.
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
        path_ = std::exchange(other.path_, {});
    }
    return *this;
}

// C++ LESSON: destructors must never throw — if one throws while the stack is
// already unwinding from another exception, the program calls std::terminate.
// So cleanup code uses the non-throwing `error_code` overloads and shrugs at
// failures (best effort is the correct posture for cleanup).
TempDir::~TempDir() {
    if (!path_.empty()) {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
}

} // namespace wirebard
