#include "json.h"

#include <format>

namespace wirebard {

std::string json_quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            // Other C0 control characters must be \u-escaped to be valid JSON;
            // everything else (including UTF-8 multibyte bytes) passes through.
            if (static_cast<unsigned char>(c) < 0x20) {
                out +=
                    std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
    return out;
}

JsonObject& JsonObject::str(std::string_view key, std::string_view value) {
    comma();
    body_ += json_quote(key);
    body_ += ':';
    body_ += json_quote(value);
    return *this;
}

JsonObject& JsonObject::boolean(std::string_view key, bool value) {
    comma();
    body_ += json_quote(key);
    body_ += ':';
    body_ += value ? "true" : "false";
    return *this;
}

JsonObject& JsonObject::raw(std::string_view key, std::string_view json) {
    comma();
    body_ += json_quote(key);
    body_ += ':';
    body_ += json;
    return *this;
}

JsonArray& JsonArray::push(std::string_view json) {
    if (!body_.empty())
        body_ += ',';
    body_ += json;
    return *this;
}

} // namespace wirebard
