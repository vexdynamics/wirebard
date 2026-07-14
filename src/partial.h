// partial.h — the data model for a loaded partial (one piece of a network's
// config) and the loader that reads + substitutes a whole network folder.
//
// A partial is deliberately opaque here: M1 only needs its substituted text
// in merge order. Parsing [Interface]/[Peer] sections (for the allocator and
// the server-conf renderer) arrives in a later milestone — this stays a thin
// "read the folder, expand the variables" layer until then.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "error.h"
#include "vars.h"

namespace wirebard {

// One loaded partial: its path and its (already ${}-substituted) text. `raw`
// is what a later render step emits verbatim.
//
// C++ LESSON: raw is an OWNING std::string on purpose. It could have been a
// string_view into the read-file buffer — faster, and a lifetime-bug factory
// the moment the buffer is freed or the Partial is moved. The rule a veteran
// follows without thinking: values for KEEPING, views for PASSING. Parse
// results get kept, so they own their bytes.
struct Partial {
    std::filesystem::path path;
    std::string raw;
};

// Load every compiled partial in `network_dir` (list_partials order — 00-main
// first), substituting `vars` into each. Zero partials is an error: an empty
// network is never what you meant.
Result<std::vector<Partial>> load_partials(const std::filesystem::path& network_dir,
                                           const VarTable& vars);

} // namespace wirebard
