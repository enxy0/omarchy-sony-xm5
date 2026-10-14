#pragma once

#include "MDRProtocol.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace testutil {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

inline bool& currentFailed() {
    static bool failed = false;
    return failed;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const std::string& message, const char* file, int line) {
    std::cerr << "    FAIL " << file << ":" << line << ": " << message << "\n";
    currentFailed() = true;
}

inline int runAll(const char* suite) {
    int passed = 0;
    for (const auto& t : registry()) {
        currentFailed() = false;
        t.fn();
        if (currentFailed()) {
            ++failures();
            std::cout << "[  FAILED  ] " << t.name << "\n";
        } else {
            ++passed;
            std::cout << "[       OK ] " << t.name << "\n";
        }
    }
    std::cout << suite << ": " << passed << "/" << registry().size() << " test cases passed\n";
    return failures() == 0 ? 0 : 1;
}

template <typename T>
std::string show(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

inline std::string show(const omarchy::sony::protocol::Bytes& value) {
    return "[" + omarchy::sony::protocol::hexBytes(value) + "]";
}

inline std::string show(uint8_t value) { return omarchy::sony::protocol::hexByte(value); }
inline std::string show(bool value) { return value ? "true" : "false"; }

template <typename T>
std::string show(const std::optional<T>& value) {
    return value ? show(*value) : std::string("nullopt");
}

// ---------------------------------------------------------------------------
// Minimal JSON parser for comparing daemon output with fixtures.
// ---------------------------------------------------------------------------
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{false};
    double number{0};
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    std::vector<std::string> keyOrder;

    [[nodiscard]] bool has(const std::string& key) const { return object.count(key) != 0; }
    [[nodiscard]] const Json& operator[](const std::string& key) const {
        static const Json null;
        auto it = object.find(key);
        return it == object.end() ? null : it->second;
    }
    [[nodiscard]] bool isNull() const { return type == Type::Null; }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    bool parse(Json& out) {
        skip();
        if (!value(out)) return false;
        skip();
        return pos_ == text_.size();
    }

private:
    void skip() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool str(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            char c = text_[pos_++];
            if (c == '\\') {
                if (pos_ >= text_.size()) return false;
                char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) return false;
                        unsigned code = std::stoul(text_.substr(pos_, 4), nullptr, 16);
                        pos_ += 4;
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20) return false;
                out.push_back(c);
            }
        }
        if (pos_ >= text_.size()) return false;
        ++pos_;
        return true;
    }

    bool value(Json& out) {
        skip();
        if (pos_ >= text_.size()) return false;
        char c = text_[pos_];
        if (c == 'n') { out.type = Json::Type::Null; return literal("null"); }
        if (c == 't') { out.type = Json::Type::Bool; out.boolean = true; return literal("true"); }
        if (c == 'f') { out.type = Json::Type::Bool; out.boolean = false; return literal("false"); }
        if (c == '"') { out.type = Json::Type::String; return str(out.string); }
        if (c == '[') {
            out.type = Json::Type::Array;
            ++pos_;
            skip();
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
            while (true) {
                Json item;
                if (!value(item)) return false;
                out.array.push_back(std::move(item));
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
                return false;
            }
        }
        if (c == '{') {
            out.type = Json::Type::Object;
            ++pos_;
            skip();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
            while (true) {
                skip();
                std::string key;
                if (!str(key)) return false;
                skip();
                if (pos_ >= text_.size() || text_[pos_] != ':') return false;
                ++pos_;
                Json item;
                if (!value(item)) return false;
                if (out.object.count(key)) return false;
                out.keyOrder.push_back(key);
                out.object.emplace(key, std::move(item));
                skip();
                if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
                return false;
            }
        }
        size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                                       text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' ||
                                       text_[pos_] == '-')) {
            ++pos_;
        }
        if (start == pos_) return false;
        out.type = Json::Type::Number;
        out.number = std::stod(text_.substr(start, pos_ - start));
        return true;
    }

    std::string text_;
    size_t pos_{0};
};

inline std::optional<Json> parseJson(const std::string& text) {
    Json out;
    JsonParser parser(text);
    if (!parser.parse(out)) return std::nullopt;
    return out;
}

inline std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::string jsonDiff(const Json& a, const Json& b, const std::vector<std::string>& ignore,
                            const std::string& path = "$") {
    if (a.type != b.type) return path + ": type differs";
    switch (a.type) {
    case Json::Type::Null: return "";
    case Json::Type::Bool: return a.boolean == b.boolean ? "" : path + ": " + show(a.boolean) + " vs " + show(b.boolean);
    case Json::Type::Number: return a.number == b.number ? "" : path + ": " + show(a.number) + " vs " + show(b.number);
    case Json::Type::String: return a.string == b.string ? "" : path + ": \"" + a.string + "\" vs \"" + b.string + "\"";
    case Json::Type::Array:
        if (a.array.size() != b.array.size()) return path + ": array length differs";
        for (size_t i = 0; i < a.array.size(); ++i) {
            auto d = jsonDiff(a.array[i], b.array[i], ignore, path + "[" + std::to_string(i) + "]");
            if (!d.empty()) return d;
        }
        return "";
    case Json::Type::Object: {
        auto ignored = [&](const std::string& k) {
            return std::find(ignore.begin(), ignore.end(), k) != ignore.end();
        };
        for (const auto& [k, v] : a.object) {
            if (ignored(k)) continue;
            if (!b.has(k)) return path + "." + k + ": missing on the right";
            auto d = jsonDiff(v, b[k], ignore, path + "." + k);
            if (!d.empty()) return d;
        }
        for (const auto& [k, v] : b.object) {
            if (!ignored(k) && !a.has(k)) return path + "." + k + ": missing on the left";
        }
        return "";
    }
    }
    return "";
}

} // namespace testutil

#define TEST(name)                                                      \
    static void name();                                                 \
    static testutil::Registrar registrar_##name(#name, name);           \
    static void name()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) testutil::fail("CHECK(" #cond ")", __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ(actual, expected)                                      \
    do {                                                                \
        const auto& a_ = (actual);                                      \
        const auto& e_ = (expected);                                    \
        if (!(a_ == e_)) {                                              \
            testutil::fail(std::string(#actual " == " #expected ": got ") + testutil::show(a_) + \
                           ", expected " + testutil::show(e_), __FILE__, __LINE__); \
        }                                                               \
    } while (0)

#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            testutil::fail("REQUIRE(" #cond ")", __FILE__, __LINE__);   \
            return;                                                     \
        }                                                               \
    } while (0)
