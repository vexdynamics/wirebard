#include "peer.h"

#include <algorithm>
#include <format>
#include <optional>

#include "net.h"

namespace wirebard {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// ASCII-lowercase compare, so "PublicKey" / "publickey" both match. WireGuard's
// keys are fixed-case, but a hand-written partial shouldn't fail on casing.
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

// A [Peer] block accumulated across lines. Emitted when the block closes (the
// next section header, or end of file).
struct Pending {
    // = {} so a designated-initializer that omits these leaves them empty
    // without tripping -Wmissing-designated-field-initializers (see error.h).
    std::optional<std::string> public_key = {}; // NOLINT(readability-redundant-member-init)
    std::optional<uint32_t> address = {};       // NOLINT(readability-redundant-member-init)
    std::string name;
    int header_line = 0;
};

} // namespace

Result<std::vector<Assignment>> collect_assignments(std::span<const Partial> partials) {
    std::vector<Assignment> out;

    for (const Partial& part : partials) {
        std::string_view text = part.raw;
        bool in_peer = false;
        std::string pending_name; // from a "# wirebard: name=" comment, next peer
        Pending cur;

        // Close the current [Peer], validating it carries both required fields.
        auto flush = [&]() -> Result<void> {
            if (!in_peer)
                return {};
            in_peer = false;
            if (!cur.public_key && !cur.address)
                return {}; // an empty [Peer] stub — nothing to record
            if (!cur.public_key || !cur.address) {
                return std::unexpected(
                    Error{.code = ErrorCode::config,
                          .message = cur.public_key
                                         ? "peer has a PublicKey but no valid AllowedIPs address"
                                         : "peer has an AllowedIPs address but no PublicKey",
                          .where = SourceLoc{.file = part.path, .line = cur.header_line}});
            }
            out.push_back(Assignment{.public_key = std::move(*cur.public_key),
                                     .name = std::move(cur.name),
                                     .address = *cur.address,
                                     .source = part.path});
            return {};
        };

        int line_no = 0;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos)
                eol = text.size();
            ++line_no;
            std::string_view line = trim(text.substr(pos, eol - pos));
            pos = eol + 1;

            if (line.empty())
                continue;

            // Metadata comment: "# wirebard: name=<label>".
            if (line.front() == '#') {
                std::string_view body = trim(line.substr(1));
                constexpr std::string_view kTag = "wirebard:";
                if (body.starts_with(kTag)) {
                    std::string_view meta = trim(body.substr(kTag.size()));
                    if (const size_t eq = meta.find('=');
                        eq != std::string_view::npos && iequals(trim(meta.substr(0, eq)), "name")) {
                        pending_name = std::string(trim(meta.substr(eq + 1)));
                    }
                }
                continue;
            }

            // Section header: "[Peer]" / "[Interface]" / other.
            if (line.front() == '[') {
                if (auto r = flush(); !r)
                    return std::unexpected(r.error());
                if (iequals(line, "[Peer]")) {
                    in_peer = true;
                    cur = Pending{.name = std::move(pending_name), .header_line = line_no};
                    pending_name.clear();
                }
                continue;
            }

            if (!in_peer)
                continue;

            // key = value inside a [Peer].
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos)
                continue;
            std::string_view key = trim(line.substr(0, eq));
            std::string_view value = trim(line.substr(eq + 1));

            if (iequals(key, "PublicKey")) {
                cur.public_key = std::string(value);
            } else if (iequals(key, "AllowedIPs")) {
                // First comma-separated entry is the peer's own address; a /32
                // (or a bare host) — take its host part.
                std::string_view first = value.substr(0, value.find(','));
                first = trim(first);
                auto host = !first.contains('/')
                                ? parse_ipv4(first)
                                : parse_cidr(first).transform([](Cidr c) { return c.addr; });
                if (!host) {
                    return std::unexpected(
                        Error{.code = ErrorCode::config,
                              .message = std::format("bad AllowedIPs '{}' in peer", value),
                              .where = SourceLoc{.file = part.path, .line = line_no}});
                }
                cur.address = *host;
            }
        }

        if (auto r = flush(); !r)
            return std::unexpected(r.error());
    }

    return out;
}

std::string render_peer_partial(std::string_view name, std::string_view public_key,
                                uint32_t address) {
    return std::format("# wirebard: name={}\n[Peer]\nPublicKey = {}\nAllowedIPs = {}/32\n", name,
                       public_key, format_ipv4(address));
}

namespace {

// Leading NN order prefix of a filename ("20-bob.conf" -> 20), or -1 if it
// doesn't start with digits followed by '-'.
int order_prefix(std::string_view filename) {
    size_t i = 0;
    while (i < filename.size() && filename[i] >= '0' && filename[i] <= '9')
        ++i;
    if (i == 0 || i >= filename.size() || filename[i] != '-')
        return -1;
    int n = 0;
    for (size_t k = 0; k < i; ++k)
        n = (n * 10) + (filename[k] - '0');
    return n;
}

// A filesystem-safe form of a peer name: keep [A-Za-z0-9._-], fold anything
// else to '-'. A name with no alphanumeric content (empty, or all
// punctuation/slashes) falls back to "peer" rather than an ugly "----".
std::string sanitize(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    bool has_alnum = false;
    for (char c : name) {
        const bool alnum =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        has_alnum = has_alnum || alnum;
        out += (alnum || c == '.' || c == '_' || c == '-') ? c : '-';
    }
    return has_alnum ? out : "peer";
}

} // namespace

std::string next_peer_filename(std::span<const std::filesystem::path> existing,
                               std::string_view name) {
    int highest = -1; // so an empty network yields 0 -> first peer at 10
    for (const auto& p : existing)
        highest = std::max(highest, order_prefix(p.filename().string()));
    const int next = (highest < 0 ? 0 : highest) + 10;
    return std::format("{:02}-{}.conf", next, sanitize(name));
}

} // namespace wirebard
