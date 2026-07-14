#include "partial.h"

#include <format>
#include <utility>

#include "directives.h"
#include "fs.h"

namespace wirebard {

Result<std::vector<Partial>> load_partials(const std::filesystem::path& network_dir,
                                           const VarTable& vars) {
    auto files = list_partials(network_dir);
    if (!files)
        return std::unexpected(files.error());
    if (files->empty()) {
        return std::unexpected(
            Error{.code = ErrorCode::io,
                  .message = std::format("no compiled *{} partials in this network (need at least "
                                         "00-main.conf)",
                                         kPartialSuffix),
                  .where = SourceLoc{.file = network_dir}});
    }

    std::vector<Partial> out;
    out.reserve(files->size());
    for (auto& file : *files) {
        auto text = read_file(file);
        if (!text)
            return std::unexpected(text.error());
        auto expanded = substitute(*text, vars, file);
        if (!expanded)
            return std::unexpected(expanded.error());
        out.push_back(Partial{.path = std::move(file), .raw = std::move(*expanded)});
    }
    return out;
}

} // namespace wirebard
