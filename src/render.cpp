#include "render.h"

#include <format>

namespace wirebard {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

bool is_interface_header(std::string_view line) {
    line = trim(line);
    if (line.size() != 11)
        return false;
    // case-insensitive "[Interface]"
    constexpr std::string_view want = "[interface]";
    for (size_t i = 0; i < want.size(); ++i) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
        if (c != want[i])
            return false;
    }
    return true;
}

} // namespace

std::string compile(std::span<const Partial> partials, const CompileOptions& opts) {
    std::string out;
    out += "# Managed by wirebard — DO NOT EDIT.\n";
    out += std::format("# Compiled from partials/{}/ — edit those, then `wirebard build`.\n",
                       opts.interface);
    if (!opts.env_label.empty())
        out += std::format("# Environment: {}\n", opts.env_label);
    out += '\n';

    // The server PrivateKey is injected exactly once, right under the first
    // [Interface] header we emit.
    bool injected = false;
    for (const Partial& p : partials) {
        out += std::format("# ===== {} =====\n", p.path.filename().string());

        std::string_view text = p.raw;
        size_t pos = 0;
        while (true) {
            size_t eol = text.find('\n', pos);
            const bool last = (eol == std::string_view::npos);
            std::string_view line = text.substr(pos, last ? std::string_view::npos : eol - pos);
            if (last && line.empty())
                break; // the partial ended exactly at a newline — no phantom line

            out += line;
            out += '\n'; // normalize every line ending, guarantee a trailing newline

            if (!injected && !opts.server_private_key.empty() && is_interface_header(line)) {
                out += "PrivateKey = ";
                out += opts.server_private_key;
                out += '\n';
                injected = true;
            }
            if (last)
                break;
            pos = eol + 1;
        }
        out += '\n'; // blank line between partial blocks
    }
    return out;
}

} // namespace wirebard
