#include "export/json_write.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

namespace swchess::exporter {
namespace {

// Reads one UTF-8 character out of `text` at `at` and moves `at` past it.
// Bytes that do not form a valid sequence come back one at a time, so a string
// that is not UTF-8 still produces output rather than throwing.
std::uint32_t nextCodepoint(const std::string& text, std::size_t& at) {
    unsigned char lead = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    std::uint32_t value = 0;
    if (lead < 0x80) {
        ++at;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        value = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        value = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        value = lead & 0x07u;
    } else {
        ++at;
        return lead;
    }
    if (at + extra >= text.size()) {
        ++at;
        return lead;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        unsigned char part = static_cast<unsigned char>(text[at + i]);
        if ((part & 0xC0) != 0x80) {
            ++at;
            return lead;
        }
        value = (value << 6) | (part & 0x3Fu);
    }
    at += extra + 1;
    return value;
}

void appendHex4(std::string& out, std::uint32_t value) {
    static const char* digits = "0123456789abcdef";
    out += "\\u";
    out += digits[(value >> 12) & 0xF];
    out += digits[(value >> 8) & 0xF];
    out += digits[(value >> 4) & 0xF];
    out += digits[value & 0xF];
}

// Escapes one string the way Python's json encoder does. With `ensureAscii`
// every character outside the printable ASCII range becomes a \u escape, and
// a character above the basic plane becomes a surrogate pair. Without it only
// the quote, the backslash and the control characters are escaped.
void appendString(std::string& out, const std::string& text, bool ensureAscii) {
    out += '"';
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t start = at;
        std::uint32_t c = nextCodepoint(text, at);
        switch (c) {
            case '\\': out += "\\\\"; continue;
            case '"': out += "\\\""; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            default: break;
        }
        if (ensureAscii) {
            if (c >= 0x20 && c <= 0x7E) {
                out += static_cast<char>(c);
            } else if (c <= 0xFFFF) {
                appendHex4(out, c);
            } else {
                std::uint32_t rest = c - 0x10000u;
                appendHex4(out, 0xD800u + (rest >> 10));
                appendHex4(out, 0xDC00u + (rest & 0x3FFu));
            }
        } else if (c <= 0x1F) {
            appendHex4(out, c);
        } else {
            out.append(text, start, at - start);
        }
    }
    out += '"';
}

void appendValue(std::string& out, const Json& value, bool ensureAscii, int level);

void appendContainer(std::string& out, const Json& value, bool ensureAscii, int level,
                     char open, char close) {
    if (value.empty()) {
        out += open;
        out += close;
        return;
    }
    out += open;
    out += '\n';
    const std::string inner(static_cast<std::size_t>(level + 1), ' ');
    const std::string outer(static_cast<std::size_t>(level), ' ');
    bool first = true;
    for (auto item = value.begin(); item != value.end(); ++item) {
        if (!first) {
            out += ",\n";
        }
        first = false;
        out += inner;
        if (open == '{') {
            appendString(out, item.key(), ensureAscii);
            out += ": ";
        }
        appendValue(out, item.value(), ensureAscii, level + 1);
    }
    out += '\n';
    out += outer;
    out += close;
}

void appendValue(std::string& out, const Json& value, bool ensureAscii, int level) {
    switch (value.type()) {
        case nlohmann::detail::value_t::null:
            out += "null";
            break;
        case nlohmann::detail::value_t::boolean:
            out += value.get<bool>() ? "true" : "false";
            break;
        case nlohmann::detail::value_t::number_integer:
            out += std::to_string(value.get<std::int64_t>());
            break;
        case nlohmann::detail::value_t::number_unsigned:
            out += std::to_string(value.get<std::uint64_t>());
            break;
        case nlohmann::detail::value_t::number_float:
            out += pythonFloatRepr(value.get<double>());
            break;
        case nlohmann::detail::value_t::string:
            appendString(out, value.get_ref<const std::string&>(), ensureAscii);
            break;
        case nlohmann::detail::value_t::array:
            appendContainer(out, value, ensureAscii, level, '[', ']');
            break;
        case nlohmann::detail::value_t::object:
            appendContainer(out, value, ensureAscii, level, '{', '}');
            break;
        default:
            throw std::runtime_error("cannot write this JSON value");
    }
}

}  // namespace

std::string pythonFloatRepr(double value) {
    // Ask for the shortest scientific form, which gives the digits and the
    // exponent separately, then lay them out the way Python does.
    // std::to_chars for doubles needs macOS 13.3, and the app declares 11.0,
    // so try each precision until the text reads back as the same double.
    // The first one that does holds the shortest digits, which is what
    // to_chars would have written.
    char buffer[64];
    std::string text;
    for (int precision = 0; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*e", precision, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    text = buffer;

    bool negative = false;
    std::size_t at = 0;
    if (text[at] == '-') {
        negative = true;
        ++at;
    }
    std::string digits;
    while (at < text.size() && text[at] != 'e') {
        if (text[at] != '.') {
            digits += text[at];
        }
        ++at;
    }
    int exponent = 0;
    if (at < text.size()) {
        exponent = std::atoi(text.c_str() + at + 1);
    }
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    if (digits == "0") {
        return negative ? "-0.0" : "0.0";
    }

    // `point` is where the decimal point sits among the digits, counted the
    // way CPython's format_float_short counts it.
    const int point = exponent + 1;
    std::string out;
    if (negative) {
        out += '-';
    }
    if (point <= -4 || point > 16) {
        out += digits[0];
        if (digits.size() > 1) {
            out += '.';
            out.append(digits, 1, std::string::npos);
        }
        out += 'e';
        int shown = point - 1;
        out += shown < 0 ? '-' : '+';
        int magnitude = shown < 0 ? -shown : shown;
        char exponentText[8];
        std::snprintf(exponentText, sizeof(exponentText), "%02d", magnitude);
        out += exponentText;
        return out;
    }
    if (point <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-point), '0');
        out += digits;
        return out;
    }
    if (static_cast<std::size_t>(point) >= digits.size()) {
        out += digits;
        out.append(static_cast<std::size_t>(point) - digits.size(), '0');
        out += ".0";
        return out;
    }
    out.append(digits, 0, static_cast<std::size_t>(point));
    out += '.';
    out.append(digits, static_cast<std::size_t>(point), std::string::npos);
    return out;
}

std::string dumpPythonJson(const Json& value, bool ensureAscii) {
    std::string out;
    appendValue(out, value, ensureAscii, 0);
    return out;
}

void writeJsonFile(const std::string& path, const Json& value, bool ensureAscii,
                   bool trailingNewline) {
    std::string text = dumpPythonJson(value, ensureAscii);
    if (trailingNewline) {
        text += '\n';
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot write " + path);
    }
    std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    if (written != text.size()) {
        throw std::runtime_error("short write on " + path);
    }
}

}  // namespace swchess::exporter
