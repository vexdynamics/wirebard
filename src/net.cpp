#include "net.h"

#include <charconv>
#include <format>

namespace wirebard {

namespace {

// Parse a decimal field into `out`, bounded to [0, max]. Rejects empty input,
// stray non-digit characters, and multi-digit values with a leading zero.
bool parse_uint(std::string_view s, unsigned max, unsigned& out) {
    if (s.empty() || s.size() > 3)
        return false;
    if (s.size() > 1 && s.front() == '0')
        return false; // leading zero: refuse the octal-looking form
    unsigned value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        return false;
    if (value > max)
        return false;
    out = value;
    return true;
}

} // namespace

Result<uint32_t> parse_ipv4(std::string_view s) {
    auto fail = [&] {
        return std::unexpected(
            Error{.code = ErrorCode::config, .message = std::format("bad IPv4 address '{}'", s)});
    };

    uint32_t addr = 0;
    int octet = 0;
    size_t start = 0;
    while (octet < 4) {
        size_t dot = s.find('.', start);
        std::string_view field =
            (dot == std::string_view::npos) ? s.substr(start) : s.substr(start, dot - start);
        unsigned value = 0;
        if (!parse_uint(field, 255, value))
            return fail();
        addr = (addr << 8) | value;
        ++octet;

        if (octet < 4) {
            if (dot == std::string_view::npos)
                return fail(); // ran out of fields early
            start = dot + 1;
        } else if (dot != std::string_view::npos) {
            return fail(); // trailing "."/extra field after the fourth octet
        }
    }
    return addr;
}

std::string format_ipv4(uint32_t addr) {
    return std::format("{}.{}.{}.{}", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                       addr & 0xFF);
}

Result<Cidr> parse_cidr(std::string_view s) {
    const size_t slash = s.find('/');
    if (slash == std::string_view::npos) {
        return std::unexpected(
            Error{.code = ErrorCode::config,
                  .message = std::format("expected CIDR 'addr/prefix', got '{}'", s)});
    }
    auto addr = parse_ipv4(s.substr(0, slash));
    if (!addr)
        return std::unexpected(addr.error());

    unsigned prefix = 0;
    if (![&] {
            std::string_view p = s.substr(slash + 1);
            if (p.empty() || p.size() > 2 || (p.size() > 1 && p.front() == '0'))
                return false;
            auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), prefix);
            return ec == std::errc{} && ptr == p.data() + p.size() && prefix <= 32;
        }()) {
        return std::unexpected(
            Error{.code = ErrorCode::config,
                  .message = std::format("bad CIDR prefix in '{}' (want /0../32)", s)});
    }
    return Cidr{.addr = *addr, .prefix = static_cast<int>(prefix)};
}

uint32_t Subnet::netmask() const {
    // A 32-bit shift is undefined behavior in C++, so /0 is special-cased.
    return prefix == 0 ? 0U : (0xFFFFFFFFU << (32 - prefix));
}

uint32_t Subnet::broadcast() const { return network | ~netmask(); }

bool Subnet::contains(uint32_t addr) const { return (addr & netmask()) == network; }

uint32_t Subnet::first_host() const { return network + 1; }

uint32_t Subnet::last_host() const { return broadcast() - 1; }

Result<Subnet> Subnet::parse(std::string_view s) {
    auto c = parse_cidr(s);
    if (!c)
        return std::unexpected(c.error());
    Subnet sub{.network = 0, .prefix = c->prefix};
    sub.network = c->addr & sub.netmask(); // mask the host bits off
    return sub;
}

} // namespace wirebard
