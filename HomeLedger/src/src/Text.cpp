// Text.cpp — реализация строковых утилит UTF-8, CSV и HTML
#include "Text.h"

#include <cctype>

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string utf8Truncate(const std::string& s, size_t chars) {
    if (utf8Length(s) <= chars) return s;
    if (chars == 0) return {};
    size_t n = 0, i = 0;
    for (; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) {
            if (n == chars - 1) break;
            ++n;
        }
    }
    return s.substr(0, i) + "…";
}

std::string padRight(const std::string& s, size_t width) {
    std::string t = utf8Truncate(s, width);
    size_t len = utf8Length(t);
    return len < width ? t + std::string(width - len, ' ') : t;
}

std::string padLeft(const std::string& s, size_t width) {
    std::string t = utf8Truncate(s, width);
    size_t len = utf8Length(t);
    return len < width ? std::string(width - len, ' ') + t : t;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string toLowerAscii(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string toLowerUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
        } else if (i + 1 < s.size() && c == 0xD0) {
            unsigned char c2 = static_cast<unsigned char>(s[i + 1]);
            if (c2 >= 0x90 && c2 <= 0x9F) {
                // Кириллица А..П (U+0410..U+041F) -> а..п (U+0430..U+043F)
                out += static_cast<char>(0xD0);
                out += static_cast<char>(c2 + 0x20);
                ++i;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                // Кириллица Р..Я (U+0420..U+042F) -> р..я (U+0440..U+044F)
                out += static_cast<char>(0xD1);
                out += static_cast<char>(c2 - 0x20);
                ++i;
            } else if (c2 == 0x81) {
                // Ё (U+0401) -> ё (U+0451)
                out += static_cast<char>(0xD1);
                out += static_cast<char>(0x91);
                ++i;
            } else if (c2 == 0x86) {
                // І (U+0406) -> і (U+0456)
                out += static_cast<char>(0xD1);
                out += static_cast<char>(0x96);
                ++i;
            } else if (c2 == 0x8E) {
                // Ў (U+040E) -> ў (U+045E)
                out += static_cast<char>(0xD1);
                out += static_cast<char>(0x9E);
                ++i;
            } else {
                out += static_cast<char>(c);
            }
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool containsIgnoreCaseUtf8(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    std::string h = toLowerUtf8(haystack);
    std::string n = toLowerUtf8(needle);
    return h.find(n) != std::string::npos;
}

bool startsWithIgnoreCaseUtf8(const std::string& s, const std::string& prefix) {
    if (prefix.empty()) return true;
    std::string ls = toLowerUtf8(s);
    std::string lp = toLowerUtf8(prefix);
    return ls.rfind(lp, 0) == 0;
}

std::vector<std::string> csvSplit(const std::string& line, char sep) {
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            fields.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

std::string csvField(const std::string& value, char sep) {
    if (value.find(sep) == std::string::npos && value.find('"') == std::string::npos &&
        value.find('\n') == std::string::npos)
        return value;
    std::string r = "\"";
    for (char c : value) {
        if (c == '"') r += '"';
        r += c;
    }
    return r + "\"";
}

std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}
