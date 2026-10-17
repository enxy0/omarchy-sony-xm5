#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omarchy::sony::json {

inline void appendEscaped(std::string& out, std::string_view text) {
    out.push_back('"');
    size_t i = 0;
    while (i < text.size()) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c == '"') { out += "\\\""; ++i; continue; }
        if (c == '\\') { out += "\\\\"; ++i; continue; }
        if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
            ++i;
            continue;
        }
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }

        size_t len = 0;
        uint32_t minCode = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; minCode = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; minCode = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; minCode = 0x10000; }

        bool valid = len > 0 && i + len <= text.size();
        uint32_t code = valid ? (c & (0xFF >> (len + 1))) : 0;
        for (size_t k = 1; valid && k < len; ++k) {
            auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) valid = false;
            code = (code << 6) | (cc & 0x3F);
        }
        if (valid && (code < minCode || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))) valid = false;

        if (valid) {
            out.append(text.substr(i, len));
            i += len;
        } else {
            out += "\\ufffd";
            ++i;
        }
    }
    out.push_back('"');
}

class Writer {
public:
    Writer& beginObject() { separator(); out_.push_back('{'); first_.push_back(true); return *this; }
    Writer& endObject() { out_.push_back('}'); first_.pop_back(); return *this; }
    Writer& beginArray() { separator(); out_.push_back('['); first_.push_back(true); return *this; }
    Writer& endArray() { out_.push_back(']'); first_.pop_back(); return *this; }

    Writer& key(std::string_view k) {
        separator();
        appendEscaped(out_, k);
        out_.push_back(':');
        afterKey_ = true;
        return *this;
    }

    Writer& null() { separator(); out_ += "null"; return *this; }
    Writer& value(bool v) { separator(); out_ += v ? "true" : "false"; return *this; }
    Writer& value(int64_t v) { separator(); out_ += std::to_string(v); return *this; }
    Writer& value(int v) { return value(static_cast<int64_t>(v)); }
    Writer& value(std::string_view v) { separator(); appendEscaped(out_, v); return *this; }
    Writer& value(const char* v) { return value(std::string_view(v)); }
    Writer& value(const std::string& v) { return value(std::string_view(v)); }

    template <typename T>
    Writer& value(const std::optional<T>& v) {
        if (!v) return null();
        return value(*v);
    }

    template <typename T>
    Writer& field(std::string_view k, const T& v) { key(k); return value(v); }
    Writer& nullField(std::string_view k) { key(k); return null(); }

    [[nodiscard]] const std::string& str() const noexcept { return out_; }

private:
    void separator() {
        if (afterKey_) { afterKey_ = false; return; }
        if (first_.empty()) return;
        if (!first_.back()) out_.push_back(',');
        first_.back() = false;
    }

    std::string out_;
    std::vector<bool> first_;
    bool afterKey_{false};
};

} // namespace omarchy::sony::json
