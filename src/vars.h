// vars.h — variable definitions (#= lines in partials) and ${...}
// substitution. Ported from haladin; the machinery is domain-agnostic (it
// knows nothing about WireGuard or HAProxy — just text and a sigil).
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"

namespace wirebard {

// The resolved variable set for one build, collected from the MAIN partial —
// the file that sorts first in a network folder, i.e. 00-main.conf. Variables
// are defined once, there, and are available to every partial; a #= line in
// any later partial is an error (see directives.h):
//
//     #= name = value          base value, any environment
//     #= prod: name = value    override for `--env prod`
//
// Two layers from that one file: base lines (later line wins per key), then
// the chosen env's qualified lines on top — think Object.assign({}, base,
// overlay). Values are the raw text after '=', trimmed: no quotes, no types.
//
// C++ LESSON: this class is the *invariant* idea made concrete. The private
// map is only ever written by load(), so once you hold a VarTable it is
// complete and immutable — every getter is `const`, and no caller can corrupt
// it. In JS you'd Object.freeze and hope; here the type system enforces it.
class VarTable {
public:
    // Scans the network folder's compiled partials (list_partials). Asking for
    // an env that no #= line mentions is an error — a typo'd env silently
    // building base values would be a terrible failure mode.
    static Result<VarTable> load(const std::filesystem::path& network_dir,
                                 std::optional<std::string_view> env);

    // Returns a view into the map's own strings — valid as long as the
    // VarTable lives. Cheap lookups, documented lifetime: the usual C++ trade.
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const;

    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& all() const noexcept {
        return values_;
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

// Replace every ${name} in `text` with its value. First undefined variable
// aborts with ErrorCode::undefined_var carrying {source_file, line}. Escape:
// `$${x}` renders as literal `${x}`. A `$` not followed by `{` is passed
// through untouched (WireGuard's own syntax never uses ${...}).
Result<std::string> substitute(std::string_view text, const VarTable& vars,
                               const std::filesystem::path& source_file);

// Every ${name} occurrence with its line — powers `check` (report ALL
// undefined vars at once, where substitute stops at the first) and `list`.
struct VarUse {
    std::string name;
    int line;
};
std::vector<VarUse> find_placeholders(std::string_view text);

} // namespace wirebard
