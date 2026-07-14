#include "directives.h"

#include "fs.h"

namespace wirebard {

namespace {

bool is_blank(char c) { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_blank(s.front()))
        s.remove_prefix(1);
    // \r too: a partial edited on Windows must not smuggle CRs into values.
    while (!s.empty() && (is_blank(s.back()) || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

} // namespace

std::vector<DirectiveLine> find_directives(std::string_view text) {
    std::vector<DirectiveLine> out;
    int line_no = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos)
            eol = text.size();
        ++line_no;
        std::string_view line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;

        // "#= " yes; "#=====" no (divider art); "#=" alone yes (empty body).
        if (line.size() < 2 || line[0] != '#' || line[1] != '=')
            continue;
        std::string_view rest = line.substr(2);
        if (!rest.empty() && !is_blank(rest.front()))
            continue;
        out.push_back(DirectiveLine{.body = trim(rest), .line = line_no});
    }
    return out;
}

Result<std::vector<std::filesystem::path>> list_partials(const std::filesystem::path& dir) {
    auto all = list_files(dir, kPartialSuffix);
    if (!all)
        return std::unexpected(all.error());

    std::vector<std::filesystem::path> out;
    out.reserve(all->size());
    for (auto& f : *all) {
        if (f.filename() == kTemplateName)
            continue; // the copy-me reference is a .conf, but never compiled
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace wirebard
