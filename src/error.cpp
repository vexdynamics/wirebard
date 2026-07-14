#include "error.h"

#include <format>

namespace wirebard {

// C++ LESSON: `const Error&` — pass by const reference. In Go/JS every
// parameter is either a copy or a reference by the type's nature; in C++ YOU
// choose at every call site's signature. The rule of thumb a veteran applies
// without thinking:
//   - cheap things (int, enum, string_view, small structs): pass by value
//   - anything that owns memory (string, vector, Error): pass by const ref
//     to avoid a deep copy you don't need
// `const` means "I promise not to modify it" and the compiler enforces it.
std::string format_error(const Error& e) {
    // std::format is C++23's template-literal equivalent (and what std::print
    // uses underneath). {} placeholders, type-checked at compile time.
    if (e.where) {
        // .string() converts a filesystem::path to text; paths are their own
        // type (they handle platform separators), not plain strings.
        if (e.where->line > 0) {
            return std::format("{}:{}: {}", e.where->file.string(), e.where->line, e.message);
        }
        return std::format("{}: {}", e.where->file.string(), e.message);
    }
    return e.message;
}

} // namespace wirebard
