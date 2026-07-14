// fs.h — small filesystem helpers: whole-file read, atomic write, temp dirs.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"

namespace wirebard {

// Read an entire file into a string. Node: fs.readFileSync. Go: os.ReadFile.
Result<std::string> read_file(const std::filesystem::path& path);

// Every regular file in `dir` whose name ends with `suffix`, sorted bytewise
// by filename. directory_iterator order is UNSPECIFIED (varies by
// filesystem!) — sorting here is what makes every consumer deterministic.
// Errors if `dir` is not a directory.
Result<std::vector<std::filesystem::path>> list_files(const std::filesystem::path& dir,
                                                      std::string_view suffix);

// Write a file so that readers NEVER see a half-written result: write to a
// temp file in the same directory, fsync, then rename(2) over the target.
// rename on POSIX is atomic — it's the only atomicity primitive the
// filesystem gives you, and every tool that writes configs for a live daemon
// (which HAProxy is!) must use this dance.
//
// C++ LESSON: std::string_view for the `contents` parameter. A string_view is
// a NON-OWNING pointer+length pair — like a Go slice header pointing into
// someone else's array. It accepts a std::string, a literal, a substring — no
// copies. The obligation that comes with it: the viewed bytes must OUTLIVE
// the view. Never store a string_view in a struct that outlives the call;
// that's C++'s classic use-after-free. Fine as a parameter (borrowed for the
// duration of the call), dangerous as a member.
Result<void> atomic_write_file(const std::filesystem::path& dest, std::string_view contents);

// Same atomic dance, but with explicit permission bits set BEFORE the rename,
// so the destination is never momentarily world-readable. Mandatory for a
// compiled interface config: it carries the server PrivateKey and must land
// 0600 (perms::owner_read | perms::owner_write).
Result<void> atomic_write_file(const std::filesystem::path& dest, std::string_view contents,
                               std::filesystem::perms mode);

// An advisory inter-process lock (flock), held for the object's lifetime. This
// is the serialization `peer add`/`remove` need: concurrent callers must not
// race the read→allocate→write→apply cycle on one network. RAII like TempDir —
// the destructor releases it on every exit path.
//
// acquire() blocks up to `timeout`, retrying, then FAILS rather than hanging
// forever (same "no unbounded wait" stance as subprocess timeouts). A timeout
// of 0 means one non-blocking attempt.
//
// C++ LESSON: this owns a file descriptor (an int handle from the OS), the
// same RAII/move-only shape as TempDir owning a path — acquire in a factory,
// release in the destructor, forbid copies (two owners closing one fd = a
// double-close bug), allow moves (transfer the fd, blank the source).
class FileLock {
public:
    static Result<FileLock> acquire(const std::filesystem::path& lockfile,
                                    std::chrono::milliseconds timeout = std::chrono::seconds{30});

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    ~FileLock();

private:
    explicit FileLock(int fd) : fd_(fd) {}
    int fd_ = -1; // -1 == moved-from / not held; destructor does nothing
};

// A scratch directory that deletes itself.
//
// C++ LESSON: RAII — Resource Acquisition Is Initialization — is THE core
// C++ idiom, and it's what replaces `defer`, `finally`, and Node's
// try/finally cleanup. The constructor acquires a resource; the DESTRUCTOR
// (~TempDir), which the compiler calls automatically when the object goes out
// of scope — on ANY path: return, error, exception — releases it. You cannot
// forget the cleanup, because leaving the scope IS the cleanup.
//
//     {
//         auto dir = TempDir::create("check");   // mkdir
//         ...use dir->path()...
//     }                                           // <- rmdir happens HERE, always
//
// This type is MOVE-ONLY: copying a TempDir would mean two objects both
// believing they own (and will delete) the same directory — double free, the
// classic C++ bug. So we delete the copy operations and allow only moves,
// which transfer ownership and leave the source empty. Same idea as Rust's
// move semantics, except C++ leaves the moved-from object in a valid "empty"
// state rather than forbidding its use.
class TempDir {
public:
    // Named constructor ("static factory") because creation can FAIL, and
    // constructors can't return errors — their only failure channel is
    // throwing. We prefer Result, hence a static function that returns one.
    static Result<TempDir> create(std::string_view prefix);

    // const member function: callable on a const TempDir, promises not to
    // mutate. noexcept: promises never to throw (lets callers/compiler relax).
    // [[nodiscard]]: calling a getter and IGNORING the result is always a bug —
    // the attribute makes the compiler say so. Put it on anything whose only
    // effect is its return value.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // The "rule of five": if you write ANY of {destructor, copy ctor, copy
    // assign, move ctor, move assign}, be explicit about all five. Most types
    // should write NONE of them (the "rule of zero") and let members handle
    // themselves; only genuine resource owners like this one need this.
    TempDir(const TempDir&) = delete; // no copying (double delete!)
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&& other) noexcept; // move: steal other's path
    TempDir& operator=(TempDir&& other) noexcept;
    ~TempDir(); // best-effort remove_all

private:
    // Private constructor: outsiders must go through create().
    explicit TempDir(std::filesystem::path p) : path_(std::move(p)) {}

    std::filesystem::path path_; // empty == moved-from, destructor does nothing
};

} // namespace wirebard
