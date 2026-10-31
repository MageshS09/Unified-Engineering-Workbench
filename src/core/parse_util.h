// Shared helpers for the STL and CSV parsers (locale-independent, allocation-free number parsing).
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace wb {

// Thrown for any rejected input file. what() is a user-facing message: what is wrong, where, what to do.
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Files above this size are refused before parsing (documented in docs/test_report.md).
constexpr std::uint64_t kMaxInputFileBytes = 200ull * 1024 * 1024;

enum class NumberStatus { Ok, Invalid, NonFinite };

// Throws ParseError with a clear message if `size` exceeds the limit.
inline void checkFileSize(std::uint64_t size, std::uint64_t limit = kMaxInputFileBytes) {
    if (size > limit) {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "The file is %.1f MB, above the %.0f MB limit. Split or decimate the data and load a smaller file.",
                      size / 1048576.0, limit / 1048576.0);
        throw ParseError(buf);
    }
}

inline std::string_view trimSpaces(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Whole-token parse: "1.5x", "", "nan" (non-finite), "1e999" (out of range) are all rejected.
inline NumberStatus parseDouble(std::string_view tok, double& out) {
    const char* b = tok.data();
    const char* e = b + tok.size();
    if (b < e && *b == '+') ++b;  // from_chars does not accept a leading '+'
    if (b == e) return NumberStatus::Invalid;
    const auto r = std::from_chars(b, e, out);
    if (r.ec != std::errc() || r.ptr != e) return NumberStatus::Invalid;
    return std::isfinite(out) ? NumberStatus::Ok : NumberStatus::NonFinite;
}

inline bool parseInt64(std::string_view tok, std::int64_t& out) {
    const char* b = tok.data();
    const char* e = b + tok.size();
    if (b < e && *b == '+') ++b;
    if (b == e) return false;
    const auto r = std::from_chars(b, e, out);
    return r.ec == std::errc() && r.ptr == e;
}

inline std::string quoted(std::string_view tok) {
    std::string s(tok.substr(0, 40));
    if (tok.size() > 40) s += "...";
    return "'" + s + "'";
}

}  // namespace wb
