// error.h — the project-wide error model.
//
// C++ LESSON: header files. C++ has no modules-by-default like JS/Go imports.
// A header is literally pasted into every file that #includes it (textual
// inclusion). `#pragma once` prevents the same header being pasted twice into
// one translation unit. The One Definition Rule (ODR) says an entity may only
// be *defined* once across the whole program — that's why headers mostly
// contain *declarations*, and function bodies live in .cpp files. (Small
// exceptions: templates, `inline` functions, and class definitions are fine
// in headers.)
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

// C++ LESSON: namespaces are like Go package names, but explicit. Everything
// we write lives in `wirebard::` so our names never collide with std:: or a
// third-party library (there is one global scope in C++ — collisions are real).
namespace wirebard {

// Every way the tool can fail, as one flat enum. Like Go, we prefer errors as
// values over exceptions; the enum gives callers something to switch on.
// Domain-specific codes get added as the tool grows — one per genuinely
// distinct failure a caller might branch on, no more.
//
// C++ LESSON: `enum class` (scoped enum) — unlike C or TypeScript enums, the
// values don't leak into the surrounding scope and don't implicitly convert
// to int. You must write ErrorCode::io, and that's a feature.
enum class ErrorCode {
    usage,           // bad CLI invocation
    io,              // file read/write/missing
    subprocess,      // fork/exec/pipe failure
    directive_parse, // malformed '#=' variable line in a partial
    undefined_var,   // ${name} with no matching #= definition
    config,          // invalid config value/state (bad address, subnet full, dup)
};

// Where in the USER'S config files a problem lives — like the file:line in a
// compiler diagnostic. This is about their configs, not our C++ source.
// line == 0 means "the whole file".
struct SourceLoc {
    std::filesystem::path file;
    int line = 0;
};

// C++ LESSON: this is an "aggregate" — a plain struct with public fields, no
// constructor. You build one with brace init, ideally with designated
// initializers (C++20), which read like a JS object literal:
//     Error{.code = ErrorCode::io, .message = "open failed"}
// Unlike JS objects, this is a VALUE: assigning or passing it copies the
// whole thing (like a Go struct, not like a JS object reference).
struct Error {
    ErrorCode code;
    std::string message;
    // optional<T> == "T or nothing", like Go's *T-as-maybe. The `= {}` is a
    // default member initializer: brace-init may omit this field, and that
    // omission is declared intentional (silences -Wmissing-field-initializers;
    // clang-tidy calls it redundant — the two tools disagree, gcc wins).
    std::optional<SourceLoc> where = {}; // NOLINT(readability-redundant-member-init)
};

// C++ LESSON: std::expected<T, E> (C++23) is Go's `(T, error)` fused into one
// value: it holds EITHER a T (success) or an E (failure), never both. Callers
// must check before use:
//
//     auto text = read_file(p);                 // Result<std::string>
//     if (!text) return std::unexpected(text.error());   // Go: if err != nil { return err }
//     use(*text);                               // Go: use(val)
//
// `*text` (or text.value()) is the success value; text.error() the failure.
// This template alias is our house convention: every fallible function in
// wirebard returns Result<T>. Functions that can't fail just return T.
template <typename T> using Result = std::expected<T, Error>;

// Analysis passes (when they arrive) shouldn't stop at the first problem —
// they collect everything and let the user fix their config in one pass.
enum class Severity { error, warning };

struct Diagnostic {
    Severity severity;
    Error error;
};

// Renders "wg0.conf:14: message" style text.
// Declared here, defined once in error.cpp (ODR in action).
std::string format_error(const Error& e);

} // namespace wirebard
