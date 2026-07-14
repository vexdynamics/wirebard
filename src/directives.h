// directives.h — the wirebard side-channel inside partial files.
//
// A partial is plain WireGuard syntax, but comment lines starting with the
// wirebard sigil carry variable definitions WireGuard only ever sees as a
// comment:
//
//     #= name = value           variable definition (used as ${name})
//     #= prod: name = value     ...only when built with --env prod
//
// The sigil must be followed by whitespace: "#=====" divider art is a plain
// comment, "#= x = 1" is a directive. This one rule is why no separate values
// file is needed — 00-main.conf carries the values, and backing up
// partials/ backs up everything.
//
// This module only FINDS directive lines; vars.cpp owns the grammar.
#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "error.h"

namespace wirebard {

// The partial-file conventions, defined once. A project mirrors
// /etc/wireguard: <root>/partials/<network>/*.conf compile (bytewise
// filename order) into <root>/<network>.conf. The folder name is the
// network name is the interface name (wg-quick@<network>).
//
// Files under a network folder that are NOT compiled: template.conf (the
// copy-me reference), server.key (the private key), and anything not ending
// in .conf. WireGuard's packaged units read only the compiled <network>.conf,
// never this directory.
inline constexpr std::string_view kPartialsDir = "partials";
inline constexpr std::string_view kPartialSuffix = ".conf";
// The copy-me reference; a .conf like any peer file, so it must be excluded
// from compilation by NAME (see partial.cpp), not by suffix.
inline constexpr std::string_view kTemplateName = "template.conf";
// The server's private key, referenced only when composing the compiled conf.
inline constexpr std::string_view kServerKeyName = "server.key";

// One directive line, located for diagnostics. `body` is everything after the
// "#=" sigil, trimmed — a VIEW into the scanned text. Safe as a member here
// ONLY because callers scan a text they hold, consume the results, and drop
// them before the text dies: views for passing, strings for keeping.
struct DirectiveLine {
    std::string_view body;
    int line; // 1-based, within the scanned text
};

// All lines whose first non-blank content is "#=" followed by whitespace
// (or nothing).
std::vector<DirectiveLine> find_directives(std::string_view text);

// Which files in a network folder wirebard compiles: every *.conf EXCEPT
// template.conf, sorted bytewise by filename (hence the NN- prefix — the
// first entry is 00-main.conf, where variables live). server.key and other
// non-.conf files are excluded by suffix. Errors if `dir` isn't a directory.
Result<std::vector<std::filesystem::path>> list_partials(const std::filesystem::path& dir);

} // namespace wirebard
