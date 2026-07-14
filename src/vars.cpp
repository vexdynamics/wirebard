#include "vars.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

#include "directives.h"
#include "fs.h"

namespace wirebard {

namespace {

// Is this character valid inside a ${identifier}? [A-Za-z0-9_]
bool ident_char(char c) {
    // std::isalnum has undefined behavior for negative chars (a real C trap);
    // casting to unsigned char first is the canonical safe incantation.
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool valid_ident(std::string_view s) { return !s.empty() && std::ranges::all_of(s, ident_char); }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

// One parsed `#=` line: [env ':'] name '=' value.
struct VarDirective {
    std::string_view env; // empty = base layer (every environment)
    std::string_view name;
    std::string_view value;
};

Result<VarDirective> parse_var_directive(std::string_view body, const std::filesystem::path& file,
                                         int line) {
    auto fail = [&](std::string msg) {
        return std::unexpected(Error{.code = ErrorCode::directive_parse,
                                     .message = std::move(msg),
                                     .where = SourceLoc{.file = file, .line = line}});
    };

    // Split on the FIRST '=' — values may contain '=' freely (base64, URLs).
    const size_t eq = body.find('=');
    if (eq == std::string_view::npos) {
        return fail("malformed variable directive — expected '#= name = value' "
                    "or '#= env: name = value'");
    }

    VarDirective d{.env = {}, .name = trim(body.substr(0, eq)), .value = trim(body.substr(eq + 1))};

    // An optional env qualifier sits left of the name: "prod: endpoint".
    if (const size_t colon = d.name.find(':'); colon != std::string_view::npos) {
        d.env = trim(d.name.substr(0, colon));
        d.name = trim(d.name.substr(colon + 1));
        if (!valid_ident(d.env)) {
            return fail(std::format("bad environment name '{}' (letters, digits, _)", d.env));
        }
    }
    if (!valid_ident(d.name)) {
        return fail(std::format("bad variable name '{}' (letters, digits, _)", d.name));
    }
    return d;
}

} // namespace

Result<VarTable> VarTable::load(const std::filesystem::path& network_dir,
                                std::optional<std::string_view> env) {
    auto files = list_partials(network_dir);
    if (!files)
        return std::unexpected(files.error());
    if (files->empty()) {
        return std::unexpected(Error{
            .code = ErrorCode::io,
            .message = std::format("no compiled *{} partials here — variables live on #= lines "
                                   "inside 00-main.conf",
                                   kPartialSuffix),
            .where = SourceLoc{.file = network_dir}});
    }

    // Variables are defined ONCE, in the main partial — the file that sorts
    // first (00-main.conf) — and are visible to every partial. One place to
    // look, no cross-file precedence to reason about. A '#=' anywhere else is
    // a loud error, not a silently-working exception.
    const std::filesystem::path& main_partial = files->front();
    for (const auto& file : *files) {
        if (file == main_partial)
            continue;
        auto text = read_file(file);
        if (!text)
            return std::unexpected(text.error());
        auto strays = find_directives(*text);
        if (!strays.empty()) {
            return std::unexpected(Error{
                .code = ErrorCode::directive_parse,
                .message = std::format("'#=' outside the main partial — variables are defined "
                                       "once, in the first partial ({}), and work in every partial",
                                       main_partial.filename().string()),
                .where = SourceLoc{.file = file, .line = strays.front().line}});
        }
    }

    // Two layers from that one file: base values and the chosen env's
    // overrides (the env layer sits on top regardless of line order).
    // insert_or_assign = "set, overwriting" — a later line wins over an
    // earlier one for the same key. (map::insert alone would KEEP the old
    // value, a classic C++ surprise.)
    std::map<std::string, std::string, std::less<>> base;
    std::map<std::string, std::string, std::less<>> overlay;
    std::set<std::string, std::less<>> envs_seen;

    auto text = read_file(main_partial);
    if (!text)
        return std::unexpected(text.error());
    for (const DirectiveLine& dl : find_directives(*text)) {
        auto d = parse_var_directive(dl.body, main_partial, dl.line);
        if (!d)
            return std::unexpected(d.error());

        if (d->env.empty()) {
            base.insert_or_assign(std::string(d->name), std::string(d->value));
        } else {
            envs_seen.emplace(d->env);
            if (env && d->env == *env) {
                overlay.insert_or_assign(std::string(d->name), std::string(d->value));
            }
        }
    }

    // A typo'd --env silently building base values would be the worst failure
    // mode this tool has — refuse instead.
    if (env && !envs_seen.contains(*env)) {
        std::string known;
        for (const auto& e : envs_seen)
            known += std::format(" {}", e);
        if (known.empty())
            known = " none";
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("--env {0} matches no '#= {0}: ...' line in 00-main.conf "
                                         "(typo? environments in use:{1})",
                                         *env, known),
                  .where = SourceLoc{.file = main_partial}});
    }

    VarTable table;
    table.values_ = std::move(base);
    for (auto& [name, value] : overlay) {
        table.values_.insert_or_assign(name, std::move(value));
    }
    return table;
}

std::optional<std::string_view> VarTable::get(std::string_view name) const {
    // Heterogeneous find (the std::less<> comparator, taught in cli.h): no
    // temporary std::string built from `name`.
    auto it = values_.find(name);
    if (it == values_.end())
        return std::nullopt;
    return it->second; // view into the map's string — lives as long as *this
}

// One scanner used by both substitute() and find_placeholders(). It walks the
// text once and invokes a callback per event.
//
// C++ LESSON: templates on callables. `Callback` is any lambda/function
// matching how we call it — resolved AT COMPILE TIME (like Go generics,
// unlike JS where everything is dynamic). No std::function overhead: the
// lambda's body is effectively inlined into the loop.
namespace {

enum class TokenKind { literal, placeholder, escaped_placeholder };

template <typename Callback> void scan_placeholders(std::string_view text, Callback&& cb) {
    int line = 1;
    size_t i = 0;
    size_t literal_start = 0;

    auto flush_literal = [&](size_t end) {
        if (end > literal_start) {
            cb(TokenKind::literal, text.substr(literal_start, end - literal_start), line);
        }
    };

    while (i < text.size()) {
        if (text[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        // "$${name}" -> escaped: emit literal "${name}"
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '$' && i + 2 < text.size() &&
            text[i + 2] == '{') {
            flush_literal(i);
            size_t close = text.find('}', i + 2);
            if (close != std::string_view::npos) {
                cb(TokenKind::escaped_placeholder, text.substr(i + 1, close - i), line);
                literal_start = close + 1;
                i = close + 1;
                continue;
            }
        }
        // "${name}"
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            size_t name_start = i + 2;
            size_t j = name_start;
            while (j < text.size() && ident_char(text[j]))
                ++j;
            if (j > name_start && j < text.size() && text[j] == '}') {
                flush_literal(i);
                cb(TokenKind::placeholder, text.substr(name_start, j - name_start), line);
                literal_start = j + 1;
                i = j + 1;
                continue;
            }
        }
        ++i;
    }
    flush_literal(text.size());
}

} // namespace

Result<std::string> substitute(std::string_view text, const VarTable& vars,
                               const std::filesystem::path& source_file) {
    std::string out;
    // Substituted text is roughly input-sized; reserving up front avoids the
    // grow-copy-grow dance. (Same instinct as preallocating a Go slice.)
    out.reserve(text.size());

    std::optional<Error> failure;
    scan_placeholders(text, [&](TokenKind kind, std::string_view piece, int line) {
        if (failure)
            return; // already failed; ignore the rest
        switch (kind) {
        case TokenKind::literal:
        case TokenKind::escaped_placeholder:
            out += piece; // escaped arrives as "${name}" already
            break;
        case TokenKind::placeholder:
            if (auto value = vars.get(piece)) {
                out += *value;
            } else {
                failure = Error{.code = ErrorCode::undefined_var,
                                .message = std::format("undefined variable ${{{}}}", piece),
                                .where = SourceLoc{.file = source_file, .line = line}};
            }
            break;
        }
    });

    if (failure)
        return std::unexpected(std::move(*failure));
    return out;
}

std::vector<VarUse> find_placeholders(std::string_view text) {
    std::vector<VarUse> uses;
    scan_placeholders(text, [&](TokenKind kind, std::string_view piece, int line) {
        if (kind == TokenKind::placeholder) {
            // The string(piece) copy is REQUIRED: `piece` views the caller's
            // text, which may die before the vector does. Views for borrowing,
            // strings for keeping.
            uses.emplace_back(std::string(piece), line);
        }
    });
    return uses;
}

} // namespace wirebard
