// json.h — a tiny, dependency-free JSON writer. wirebard emits JSON for
// machine callers (baki over SSH); we hand-roll it rather than pull a library,
// because the surface is small and correct string escaping is the only hard
// part — exactly the kind of pure logic worth owning and unit-testing.
//
// Not a parser, not a DOM: an ordered *writer*. Insertion order is preserved so
// output is stable and diffable (determinism, again).
#pragma once

#include <string>
#include <string_view>

namespace wirebard {

// A JSON string literal WITH surrounding quotes, escaping ", \, the control
// characters, and \n/\r/\t. Multi-line values (a rendered client_config) round
// -trip through this safely.
std::string json_quote(std::string_view s);

// Builds a JSON object, fields in the order added. dump() is const and
// repeatable — the builder holds the body, not a half-serialized buffer.
class JsonObject {
public:
    JsonObject& str(std::string_view key, std::string_view value); // "key":"escaped"
    JsonObject& boolean(std::string_view key, bool value);         // "key":true/false
    JsonObject& raw(std::string_view key, std::string_view json);  // "key":<verbatim JSON>

    [[nodiscard]] std::string dump() const { return "{" + body_ + "}"; }

private:
    void comma() {
        if (!body_.empty())
            body_ += ',';
    }
    std::string body_;
};

// Builds a JSON array of already-serialized values.
class JsonArray {
public:
    JsonArray& push(std::string_view json); // verbatim JSON element

    [[nodiscard]] std::string dump() const { return "[" + body_ + "]"; }

private:
    std::string body_;
};

} // namespace wirebard
